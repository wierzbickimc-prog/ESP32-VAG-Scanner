import KWCore
import Combine
import SwiftUI

struct HealthChecksView: View {
    @EnvironmentObject var model: AppModel
    @StateObject private var health = HealthChecks()
    private let timer = Timer.publish(every: 1, on: .main, in: .common).autoconnect()

    private var color: Color {
        switch health.result.status {
        case Int32(KW_HEALTH_READY.rawValue), Int32(KW_HEALTH_WITHIN_LIMITS.rawValue): return .ok
        case Int32(KW_HEALTH_ATTENTION.rawValue): return .fault
        default: return .warn
        }
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                HStack {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("EuroVan health checks").font(.largeTitle.bold())
                        Text(health.status).font(.callout).foregroundStyle(Color.textMuted)
                    }
                    Spacer()
                    if health.running {
                        Button("Stop") { health.stopReading() }.tint(.fault)
                    } else {
                        Button("Start") { health.start(model: model) }
                            .disabled(model.busy || model.port.isEmpty)
                    }
                }
                .buttonStyle(.borderedProminent).controlSize(.large)

                Picker("Check", selection: $health.check) {
                    ForEach(0..<Int(KW_HEALTH_COUNT.rawValue), id: \.self) { i in
                        Text(String(cString: kw_health_procedure(Int32(i))!.pointee.title)).tag(i)
                    }
                }
                .disabled(health.running)
                .onChange(of: health.check) { _ in health.selectCheck() }

                VStack(alignment: .leading, spacing: 12) {
                    Text(String(cString: health.procedure.description)).font(.headline)
                    Text(String(cString: health.procedure.preparation))
                    ForEach(Array(health.confirmationLabels.enumerated()), id: \.offset) { i, label in
                        Toggle(label, isOn: Binding(get: {
                            health.confirmations & (1 << i) != 0
                        }, set: { checked in
                            if checked { health.confirmations |= 1 << i }
                            else { health.confirmations &= ~(1 << i) }
                            health.reevaluate()
                        }))
                        .toggleStyle(.checkbox)
                        .disabled(!health.running)
                    }
                    Text("Confirmations apply to this run and must be checked against the vehicle.")
                        .font(.caption).foregroundStyle(Color.textMuted)
                }.card()

                if let sampledAt = health.sampledAt {
                    VStack(alignment: .leading, spacing: 12) {
                        Chip(text: health.running ? health.resultStatus : "Last sample · \(health.resultStatus)", color: color)
                        Text(healthText(health.result.message))
                        Text(healthText(health.result.readings))
                            .font(.system(.body, design: .monospaced)).textSelection(.enabled)
                        Text("Controller: \(health.controller)").font(.caption).textSelection(.enabled)
                        Text("Sampled \(sampledAt.formatted(date: .abbreviated, time: .standard)) · Observation \(Int(health.result.observed_s)) s")
                            .font(.caption).foregroundStyle(Color.textMuted)
                        if health.check == Int(KW_HEALTH_ATF.rawValue)
                            && health.running && health.result.status == Int32(KW_HEALTH_READY.rawValue) {
                            HStack {
                                Button("Record continued dripping") {
                                    health.save(physicalObservation: "User observed continued dripping after the initial overflow-tube fluid.")
                                }
                                Button("Record no further flow") {
                                    health.save(physicalObservation: "User observed no further flow after the initial overflow-tube fluid; topping up is indicated by the procedure.")
                                }
                            }
                        }
                        Button("Save snapshot") { health.save() }
                    }.card()
                }

                VStack(alignment: .leading, spacing: 10) {
                    Text(String(cString: health.procedure.limitations)).foregroundStyle(Color.textMuted)
                    Link(String(cString: health.procedure.source_title), destination: URL(string: String(cString: health.procedure.source_url))!)
                    Text("Mixture/fuel-trim, oxygen-sensor, warm-up and ABS checks are pending AXK/controller-specific field and limit verification. Basic Settings and actuator tests are not part of these passive checks.")
                        .font(.caption).foregroundStyle(Color.textMuted)
                }.card()

                if !health.reports.isEmpty {
                    Text("Saved observations").font(.title2.bold())
                    ForEach(health.reports) { report in
                        VStack(alignment: .leading, spacing: 8) {
                            Text("\(report.demo ? "SIMULATION · " : "")\(report.check)").font(.headline)
                            Text("\(report.date.formatted()) · \(report.status)").font(.caption)
                            Text(report.physicalObservation ?? report.message)
                            DisclosureGroup("Measurements and confirmed conditions") {
                                VStack(alignment: .leading, spacing: 8) {
                                    Text(report.controller)
                                    Text(report.readings)
                                    Text(report.confirmations.joined(separator: "\n"))
                                    Link("Procedure source", destination: URL(string: report.source)!)
                                }.font(.caption).textSelection(.enabled)
                            }
                        }.card()
                    }
                }
            }.padding(28)
        }
        .navigationTitle("Health checks")
        .onReceive(timer) { _ in if health.running { health.reevaluate() } }
        .onDisappear { health.stopReading() }
    }
}
