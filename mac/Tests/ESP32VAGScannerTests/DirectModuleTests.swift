import XCTest
import KWCore
@testable import ESP32VAGScanner

final class DirectModuleTests: XCTestCase {
    func testDirectReadWithoutScanReturnsIdentityAndFaults() async throws {
        let kline = KLine()
        defer { kline.close() }
        let result = try await kline.openModule(port: "demo:eurovan", baud: 9600, address: 0x01)
        XCTAssertEqual(result.module.address, 0x01)
        XCTAssertEqual(result.module.protocolVersion, 1281)
        XCTAssertFalse(result.module.ident.isEmpty)
        XCTAssertGreaterThan(try XCTUnwrap(result.faults).count, 0)
        XCTAssertNil(result.error)
    }

    func testMissingModuleDoesNotPreventReadingAnotherModule() async throws {
        let kline = KLine()
        defer { kline.close() }
        do {
            _ = try await kline.openModule(port: "demo:golf", baud: 9600, address: 0x02)
            XCTFail("The simulated Golf has no transmission controller")
        } catch KLineError.protocolError(let code) {
            XCTAssertEqual(code, KW_ENOWAKE)
        }
        // More modules includes 46, which is outside the six landing cards.
        let result = try await kline.openModule(port: "demo:golf", baud: 9600, address: 0x46)
        XCTAssertEqual(result.module.address, 0x46)
        XCTAssertNotNil(result.faults)
        XCTAssertNil(result.error)
    }

    func testUnsupportedProtocolIsDistinctFromNoFaultsAndRecovers() async throws {
        let kline = KLine()
        defer { kline.close() }
        let result = try await kline.openModule(port: "demo:golf", baud: 9600, address: 0x15)
        XCTAssertFalse(result.module.supported)
        XCTAssertGreaterThanOrEqual(result.module.protocolVersion, 2000)
        XCTAssertNil(result.faults)
        XCTAssertNotNil(result.error)
        let next = try await kline.openModule(port: "demo:golf", baud: 9600, address: 0x17)
        XCTAssertTrue(next.module.supported)
        XCTAssertNotNil(next.faults)
        XCTAssertNil(next.error)
    }

    func testDirectReadRefreshesFaultsAfterClearAndCableChange() async throws {
        let kline = KLine()
        defer { kline.close() }
        _ = try await kline.openModule(port: "demo:eurovan", baud: 9600, address: 0x01)
        _ = try await kline.clearFaults(port: "demo:eurovan", baud: 9600, address: 0x01)
        let cleared = try await kline.openModule(port: "demo:eurovan", baud: 9600, address: 0x01)
        XCTAssertEqual(cleared.faults?.count, 0)
        let otherCar = try await kline.openModule(port: "demo:harlequin", baud: 9600, address: 0x01)
        XCTAssertGreaterThan(try XCTUnwrap(otherCar.faults).count, 0)
    }
    @MainActor
    func testDirectNavigationUpdatesOneCardWithoutMarkingCarScanned() async throws {
        let model = AppModel()
        defer { model.kline.close() }
        model.port = "demo:eurovan"
        model.openModule(address: 0x01)
        XCTAssertEqual(model.selection?.address, 0x01)
        XCTAssertTrue(model.busy)
        try await waitUntilIdle(model)
        XCTAssertFalse(model.scanned)
        XCTAssertEqual(model.modules.map(\.address), [0x01])
        XCTAssertNotNil(model.faults[0x01])
        model.openModule(address: 0x01)
        try await waitUntilIdle(model)
        XCTAssertEqual(model.modules.count, 1)
        model.port = "demo:golf"
        XCTAssertTrue(model.modules.isEmpty)
        XCTAssertTrue(model.faults.isEmpty)
        XCTAssertNil(model.selection)
    }

    @MainActor
    func testFailedReadCanRetryAndReturningHomeDoesNotNavigateBack() async throws {
        let model = AppModel()
        defer { model.kline.close() }
        model.port = "demo:golf"
        model.openModule(address: 0x02)
        try await waitUntilIdle(model)
        XCTAssertNil(model.faults[0x02])
        XCTAssertNotNil(model.moduleErrors[0x02])
        XCTAssertTrue(model.modules.isEmpty)
        model.openModule(address: 0x01)
        model.selection = nil
        try await waitUntilIdle(model)
        XCTAssertNil(model.selection)
        XCTAssertNotNil(model.faults[0x01])
    }

    @MainActor
    private func waitUntilIdle(_ model: AppModel) async throws {
        let deadline = Date().addingTimeInterval(10)
        while model.busy && Date() < deadline {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        XCTAssertFalse(model.busy, "Module read did not finish")
    }

}
