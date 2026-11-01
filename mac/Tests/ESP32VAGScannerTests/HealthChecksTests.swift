import XCTest
import KWCore
@testable import ESP32VAGScanner

@MainActor
final class HealthChecksTests: XCTestCase {
    private func wait(_ condition: () -> Bool, seconds: Double = 12) async throws {
        let deadline = Date().addingTimeInterval(seconds)
        while !condition() && Date() < deadline {
            try await Task.sleep(nanoseconds: 20_000_000)
        }
        XCTAssertTrue(condition(), "Health run did not reach the expected state")
    }

    func testTimingRunStopsAndReturnsTheCableWithoutClearingFaults() async throws {
        let model = AppModel()
        defer { model.kline.close() }
        model.port = "demo:eurovan"
        let health = HealthChecks()
        health.check = Int(KW_HEALTH_TIMING.rawValue)
        health.confirmations = 7
        health.start(model: model)
        XCTAssertTrue(model.busy)
        XCTAssertTrue(health.running)
        XCTAssertEqual(health.confirmations, 0, "Each run needs new physical confirmations")
        try await wait { health.sampledAt != nil }
        XCTAssertTrue(health.controller.contains("022906032AF"))
        health.confirmations = 7
        health.reevaluate()
        try await wait { health.result.status == Int32(KW_HEALTH_WITHIN_LIMITS.rawValue) }
        XCTAssertTrue(healthText(health.result.readings).contains("Difference 1.0"))
        health.stopReading()
        try await wait { !model.busy }
        XCTAssertFalse(health.running)
        let module = try await model.kline.openModule(port: model.port, baud: model.baud, address: 1)
        XCTAssertEqual(module.faults?.count, 3, "Health mode must not clear demo faults or leak into normal reads")
    }

    func testATFPhysicalObservationRequiresLiveReadinessAndPreservesItsEvidence() async throws {
        let oldReports = UserDefaults.standard.data(forKey: "eurovanHealthReports")
        defer { UserDefaults.standard.set(oldReports, forKey: "eurovanHealthReports") }
        let model = AppModel()
        defer { model.kline.close() }
        model.port = "demo:eurovan"
        let health = HealthChecks()
        let before = health.reports.count
        health.start(model: model)
        try await wait { health.sampledAt != nil }
        health.save(physicalObservation: "Should not be saved before prerequisites")
        XCTAssertEqual(health.reports.count, before)
        health.confirmations = 15
        health.reevaluate()
        try await wait { health.result.status == Int32(KW_HEALTH_READY.rawValue) }
        health.save(physicalObservation: "User observed continued dripping")
        let report = try XCTUnwrap(health.reports.first)
        XCTAssertEqual(report.physicalObservation, "User observed continued dripping")
        XCTAssertTrue(report.demo)
        XCTAssertEqual(report.confirmations.count, 4)
        XCTAssertTrue(report.controller.contains("01P927733CG"))
        XCTAssertTrue(report.readings.contains("ATF"))
        let savedCount = health.reports.count
        health.stopReading()
        try await wait { !model.busy }
        health.save(physicalObservation: "Must not record a physical result after stopping")
        XCTAssertEqual(health.reports.count, savedCount)
        health.start(model: model)
        XCTAssertEqual(health.confirmations, 0)
        health.stopReading()
        try await wait { !model.busy }
    }

    func testWrongControllerEndsAsUnsupportedAndCanRetryOnEuroVan() async throws {
        let model = AppModel()
        defer { model.kline.close() }
        model.port = "demo:golf"
        let health = HealthChecks()
        health.check = Int(KW_HEALTH_TIMING.rawValue)
        health.start(model: model)
        try await wait { !model.busy }
        XCTAssertEqual(health.result.status, Int32(KW_HEALTH_UNSUPPORTED.rawValue))
        XCTAssertFalse(health.running)
        model.port = "demo:eurovan"
        health.start(model: model)
        try await wait { health.sampledAt != nil }
        XCTAssertNotEqual(health.result.status, Int32(KW_HEALTH_UNSUPPORTED.rawValue))
        health.stopReading()
        try await wait { !model.busy }
    }
}
