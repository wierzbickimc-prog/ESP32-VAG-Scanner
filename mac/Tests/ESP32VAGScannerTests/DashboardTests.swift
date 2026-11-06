import XCTest
import KWCore
@testable import ESP32VAGScanner

@MainActor
final class DashboardTests: XCTestCase {
    func testConnectingAndFailurePreserveTheLastSuccessfulSample() throws {
        let dash = Dashboard()
        dash.slots = DashSlot.defaults
        let progress = DashReading(address: 2, group: 0, err: 0, values: [], ident: "", connecting: true)
        dash.apply(progress)
        XCTAssertTrue(dash.states[2].connecting)
        XCTAssertNil(dash.states[2].lastUpdate)
        XCTAssertNil(dash.states[2].value)
        XCTAssertNil(dash.states[2].error)

        let sample = DashReading(address: 2, group: 5, err: 0,
                                 values: [SensorValue(sensor_id: 5, a: 10, b: 175)], ident: "01P927733CG")
        dash.apply(sample)
        XCTAssertEqual(dash.states[2].value, 75)
        let timestamp = try XCTUnwrap(dash.states[2].lastUpdate)
        XCTAssertFalse(dash.states[2].connecting)

        dash.apply(DashReading(address: 2, group: 5, err: KW_ENOWAKE, values: [], ident: ""))
        XCTAssertEqual(dash.states[2].value, 75)
        XCTAssertEqual(dash.states[2].lastUpdate, timestamp)
        XCTAssertNotNil(dash.states[2].error)
        dash.apply(progress)
        XCTAssertTrue(dash.states[2].connecting)
        XCTAssertEqual(dash.states[2].lastUpdate, timestamp)
        XCTAssertNotNil(dash.states[2].error)
        dash.apply(sample)
        XCTAssertNil(dash.states[2].error)
        XCTAssertFalse(dash.states[2].connecting)
        XCTAssertGreaterThanOrEqual(try XCTUnwrap(dash.states[2].lastUpdate), timestamp)
    }

    func testFuelFreshnessRequiresBothInputsAndKeepsTheOlderTimestamp() throws {
        let dash = Dashboard()
        dash.slots = DashSlot.defaults
        let maf = DashReading(address: 1, group: 2, err: 0, values: [
            SensorValue(), SensorValue(), SensorValue(), SensorValue(sensor_id: 25, a: 0, b: 20)
        ], ident: "")
        let speed = DashReading(address: 1, group: 5, err: 0, values: [
            SensorValue(), SensorValue(), SensorValue(sensor_id: 7, a: 100, b: 45)
        ], ident: "")
        dash.apply(maf)
        XCTAssertNil(dash.states[3].lastUpdate)
        dash.apply(speed)
        let timestamp = try XCTUnwrap(dash.states[3].lastUpdate)
        dash.apply(speed)
        XCTAssertEqual(dash.states[3].lastUpdate, timestamp, "New speed must not disguise old air-mass data")
    }
}
