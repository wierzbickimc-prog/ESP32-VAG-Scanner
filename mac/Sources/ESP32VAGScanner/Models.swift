import AppKit
import Foundation
import KWCore

struct Module: Identifiable, Hashable {
    let address: Int
    let ident: String
    let protocolVersion: Int

    var id: Int { address }
    var hexAddress: String { String(format: "%02X", address) }
    var name: String {
        kw_module_name(Int32(address)).map { String(cString: $0) } ?? "Controller \(hexAddress)"
    }
    var supported: Bool { protocolVersion < 2000 }
    var identLines: [String] {
        ident.split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }
    }
}

struct FaultCode: Identifiable, Hashable {
    let dtc: Int
    let status: Int
    let text: String
    /// From the user's fault code table, if loaded and listed.
    let description: String?
    /// OBD-II equivalent for engine codes from 16384 up, e.g. "P0113".
    let pcode: String?
    /// What the controller says is wrong, from the status byte, e.g. "Short to Ground".
    let type: String?

    var id: String { text }
    var code: String { String(format: "%05d", dtc) }
    var statusText: String { String(format: "%02d-%02d", status & 0x7F, (status >> 7) * 10) }
    var intermittent: Bool { status & 0x80 != 0 }

    /// Ross-Tech documents ~1,000 of the 5-digit VAG codes; the page name is the zero-padded code.
    var wikiURL: URL {
        URL(string: String(format: "https://wiki.ross-tech.com/wiki/index.php/%05d", dtc))!
    }

    init(_ raw: KWCore.FaultCode) {
        dtc = Int(raw.dtc)
        status = Int(raw.status)
        var copy = raw
        var buf = [CChar](repeating: 0, count: 32)
        kw_format_fault(&copy, &buf, 32)
        text = String(cString: buf)
        description = kw_fault_describe(raw.dtc).map { String(cString: $0) }
        var pbuf = [CChar](repeating: 0, count: 8)
        pcode = kw_fault_pcode(raw.dtc, &pbuf, 8) != 0 ? String(cString: pbuf) : nil
        type = kw_fault_type(raw.status).map { String(cString: $0) }
    }
}

/// The owner's cars, recognised by kwcore/car_profile.c from controller idents.
enum Car: Int, CaseIterable, Identifiable {
    case unknown = 0, eurovan, harlequin

    var id: Int { rawValue }

    private var profile: KwCarProfile { kw_car_profile(KwCar(rawValue: UInt32(rawValue))).pointee }
    var name: String { String(cString: profile.name) }
    var detail: String { String(cString: profile.detail) }
    var key: String { String(cString: profile.key) }

    var heroImage: NSImage? {
        Bundle.module.url(forResource: "hero_\(key)", withExtension: "jpg").flatMap(NSImage.init(contentsOf:))
    }

    static func identify(_ modules: [Module]) -> Car {
        var votes = KwCarVotes()
        for module in modules where module.supported {
            kw_car_vote(&votes, module.ident)
        }
        return Car(rawValue: Int(kw_car_best(&votes).rawValue)) ?? .unknown
    }
}

struct MeasuredValue: Hashable {
    let label: String
    let value: String
}

/// Measuring-block names from Resources/labels.csv (copied from assets/labels.csv).
enum Labels {
    static func load() {
        guard let url = Bundle.module.url(forResource: "labels", withExtension: "csv"),
              let data = try? Data(contentsOf: url) else { return }
        data.withUnsafeBytes { raw in
            _ = kw_labels_load(raw.bindMemory(to: CChar.self).baseAddress, raw.count)
        }
    }
}

/// Loads the optional fault code descriptions (code,meaning CSV) into kwcore.
enum FaultTable {
    static let url = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("ESP32 VAG Scanner/fault-codes.csv")

    /// Returns the number of descriptions loaded (0 if the file is missing).
    @discardableResult
    static func load() -> Int {
        guard let data = try? Data(contentsOf: url) else { return 0 }
        return data.withUnsafeBytes { raw in
            Int(kw_fault_table_load(raw.bindMemory(to: CChar.self).baseAddress, raw.count))
        }
    }
}

/// Photo credits from Resources/CREDITS.md (copied there by assets/tools/make_assets.py).
enum Credits {
    static var lines: [String] {
        guard let url = Bundle.module.url(forResource: "CREDITS", withExtension: "md"),
              let markdown = try? String(contentsOf: url, encoding: .utf8) else { return [] }
        return markdown.split(separator: "\n")
            .filter { $0.hasPrefix("| ") && !$0.hasPrefix("| Image") }
            .compactMap { row in
                let cells = row.split(separator: "|").map { $0.trimmingCharacters(in: .whitespaces) }
                return cells.count >= 3 ? "\(cells[0]): \(cells[1]), \(cells[2])" : nil
            }
    }
}

/// Always available on the overview, before discovery.
let landingAddresses = [0x01, 0x02, 0x03, 0x08, 0x15, 0x17]

/// Addresses probed by a quick scan (hex, as VCDS writes them).
let commonAddresses = [0x01, 0x02, 0x03, 0x08, 0x09, 0x15, 0x16, 0x17, 0x19, 0x25,
                       0x35, 0x36, 0x37, 0x45, 0x46, 0x55, 0x56, 0x57]
