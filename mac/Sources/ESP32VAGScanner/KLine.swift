import Foundation
import KWCore

enum KLineError: LocalizedError {
    case noPort
    case open(String)
    case protocolError(Int32)

    var errorDescription: String? {
        switch self {
        case .noPort: return "No cable selected."
        case .open(let port): return "Could not open \(port)."
        case .protocolError(let code):
            if code == KW_ENOWAKE { return "No response. Check the ignition and module address, then retry." }
            return String(cString: kw_strerror(code)).capitalizedFirst
        }
    }
}

/// Lets the UI stop a long-running loop on the K-line queue.
final class StopFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var stopped = false

    func stop() { lock.withLock { stopped = true } }
    var isStopped: Bool { lock.withLock { stopped } }
}

/// Owns the cable. Every kwcore call runs on one serial queue, so the UI never
/// blocks and two operations never share the bus.
final class KLine: @unchecked Sendable {
    private let queue = DispatchQueue(label: "esp32vagscanner.kline")
    private var io: UnsafeMutablePointer<KwIo>?
    private var openPort: String?
    private var openBaud = 0
    private var demoCar: UnsafeMutablePointer<FakeEcu>?
    private let session = UnsafeMutablePointer<KwSession>.allocate(capacity: 1)

    /// Simulated cars from kwcore/fake_ecu.c, for trying the app without one.
    static let demoPorts: [String: (name: String, car: FakeCar)] = [
        "demo:eurovan": ("Demo: EuroVan", FAKE_CAR_EUROVAN),
        "demo:harlequin": ("Demo: Golf Harlequin", FAKE_CAR_HARLEQUIN),
        "demo:golf": ("Demo: Golf IV", FAKE_CAR_GOLF4),
    ]

    static func isDemo(_ port: String) -> Bool { port.hasPrefix("demo:") }

    static func displayName(_ port: String) -> String {
        demoPorts[port]?.name ?? port.replacingOccurrences(of: "/dev/cu.", with: "")
    }

    static func availablePorts() -> [String] {
        let entries = (try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? []
        return entries.filter { $0.hasPrefix("cu.usbserial") }.sorted().map { "/dev/" + $0 }
            + ["demo:eurovan", "demo:harlequin", "demo:golf"]
    }

    /// The simulated car answers instantly; pace it so the UI behaves as it will on a real bus.
    private func demoPause(_ seconds: Double) {
        if let openPort, Self.isDemo(openPort) { Thread.sleep(forTimeInterval: seconds) }
    }

    // MARK: Operations

    func scan(port: String, baud: Int, addresses: [Int], stop: StopFlag,
              progress: @escaping @Sendable (Int) -> Void) async throws -> [Module] {
        try await run {
            try self.ensureOpen(port: port, baud: baud)
            var found: [Module] = []
            for address in addresses {
                if stop.isStopped { break }
                progress(address)
                self.demoPause(0.25)
                let rc = kw_connect_attempts(self.session, self.io, UInt8(address), 1)
                if rc == KW_OK {
                    found.append(self.currentModule())
                    kw_end(self.session)
                } else if rc == KW_EKWP2000 {
                    found.append(Module(address: address, ident: "", protocolVersion: Int(self.session.pointee.protocol)))
                }
            }
            return found
        }
    }

    /// Identification and faults use one wake-up, with no discovery scan.
    func openModule(port: String, baud: Int, address: Int) async throws
        -> (module: Module, faults: [FaultCode]?, error: String?) {
        try await run {
            try self.ensureOpen(port: port, baud: baud)
            self.demoPause(0.6)
            let rc = kw_connect(self.session, self.io, UInt8(address))
            guard rc == KW_OK || rc == KW_EKWP2000 else { throw KLineError.protocolError(rc) }
            let module = self.currentModule()
            guard rc == KW_OK else {
                return (module, nil, "KWP\(module.protocolVersion) is not supported.")
            }
            defer { kw_end(self.session) }
            var codes = [KWCore.FaultCode](repeating: KWCore.FaultCode(), count: 64)
            let n = kw_read_fault_codes(self.session, &codes, 64)
            guard n >= 0 else {
                return (module, nil, KLineError.protocolError(n).localizedDescription)
            }
            return (module, codes.prefix(Int(n)).map(FaultCode.init), nil)
        }
    }

    func readFaults(port: String, baud: Int, address: Int) async throws -> [FaultCode] {
        try await run {
            try self.withSession(port: port, baud: baud, address: address) { s in
                var codes = [KWCore.FaultCode](repeating: KWCore.FaultCode(), count: 64)
                let n = kw_read_fault_codes(s, &codes, 64)
                guard n >= 0 else { throw KLineError.protocolError(n) }
                return codes.prefix(Int(n)).map(FaultCode.init)
            }
        }
    }

    func clearFaults(port: String, baud: Int, address: Int) async throws -> [FaultCode] {
        try await run {
            try self.withSession(port: port, baud: baud, address: address) { s in
                var codes = [KWCore.FaultCode](repeating: KWCore.FaultCode(), count: 64)
                let n = kw_clear_fault_codes(s, &codes, 64)
                guard n >= 0 else { throw KLineError.protocolError(n) }
                return codes.prefix(Int(n)).map(FaultCode.init)
            }
        }
    }

    /// Reads `group` repeatedly in one session until `stop` is set (or once if `once`).
    /// Each value comes with its label (kwcore/labels.c).
    func readGroup(port: String, baud: Int, address: Int, group: Int, once: Bool, stop: StopFlag,
                   onValues: @escaping @Sendable ([MeasuredValue]) -> Void) async throws {
        try await run {
            try self.withSession(port: port, baud: baud, address: address) { s in
                repeat {
                    var values = [SensorValue](repeating: SensorValue(), count: 16)
                    let n = kw_group_read(s, Int32(group), &values, 16)
                    guard n >= 0 else { throw KLineError.protocolError(n) }
                    let ident = withUnsafeBytes(of: &s.pointee.ident) { raw in
                        String(cString: raw.bindMemory(to: CChar.self).baseAddress!)
                    }
                    onValues(values.prefix(Int(n)).enumerated().map { i, v in
                        MeasuredValue(label: String(cString: kw_label(ident, Int32(group), Int32(i + 1), v.sensor_id)),
                                      value: Self.format(v))
                    })
                    if !once { Thread.sleep(forTimeInterval: Self.isDemo(port) ? 0.5 : 0.2) }
                } while !once && !stop.isStopped
            }
        }
    }

    func close() {
        queue.async { self.closePort() }
    }

    // MARK: Internals

    private func run<T>(_ work: @escaping () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in
            queue.async { continuation.resume(with: Result { try work() }) }
        }
    }

    private func ensureOpen(port: String, baud: Int) throws {
        guard !port.isEmpty else { throw KLineError.noPort }
        if openPort == port && openBaud == baud, io != nil { return }
        closePort()
        let newIO = UnsafeMutablePointer<KwIo>.allocate(capacity: 1)
        newIO.initialize(to: KwIo())
        if let demo = Self.demoPorts[port] {
            let car = UnsafeMutablePointer<FakeEcu>.allocate(capacity: 1)
            fake_ecu_init_car(car, newIO, demo.car)
            car.pointee.live = 1
            demoCar = car
            io = newIO
            openPort = port
            openBaud = baud
            return
        }
        guard kw_serial_open(newIO, port, Int32(baud)) == 0 else {
            newIO.deallocate()
            throw KLineError.open(port)
        }
        kw_reset_interface(newIO)
        io = newIO
        openPort = port
        openBaud = baud
    }

    private func closePort() {
        guard let io else { return }
        if let demoCar {
            demoCar.deallocate()
            self.demoCar = nil
        } else {
            kw_serial_close(io)
        }
        io.deallocate()
        self.io = nil
        openPort = nil
    }

    private func withSession<T>(port: String, baud: Int, address: Int,
                                _ body: (UnsafeMutablePointer<KwSession>) throws -> T) throws -> T {
        try ensureOpen(port: port, baud: baud)
        demoPause(0.6)
        let rc = kw_connect(session, io, UInt8(address))
        guard rc == KW_OK else { throw KLineError.protocolError(rc) }
        defer { kw_end(session) }
        return try body(session)
    }

    private func currentModule() -> Module {
        let ident = withUnsafeBytes(of: &session.pointee.ident) { raw in
            String(cString: raw.bindMemory(to: CChar.self).baseAddress!)
        }
        return Module(address: Int(session.pointee.address),
                      ident: ident.trimmingCharacters(in: .whitespacesAndNewlines),
                      protocolVersion: Int(session.pointee.protocol))
    }

    private static func format(_ value: SensorValue) -> String {
        var v = value
        var buf = [CChar](repeating: 0, count: 64)
        kw_format_sensor(&v, &buf, 64)
        return String(cString: buf)
    }
}

private extension String {
    var capitalizedFirst: String { prefix(1).uppercased() + dropFirst() }
}
