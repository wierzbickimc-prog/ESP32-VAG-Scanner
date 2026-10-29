// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "ESP32VAGScanner",
    platforms: [.macOS(.v13)],
    targets: [
        // Symlinks into ../kwcore: the same protocol core the ESP32-P4 build uses.
        .target(
            name: "KWCore",
            path: "Sources/KWCore",
            cSettings: [.unsafeFlags(["-Wno-unused-parameter"])]
        ),
        .executableTarget(
            name: "ESP32VAGScanner",
            dependencies: ["KWCore"],
            path: "Sources/ESP32VAGScanner",
            resources: [.process("Resources")]
        ),
        .testTarget(
            name: "ESP32VAGScannerTests",
            dependencies: ["ESP32VAGScanner", "KWCore"]
        ),
    ]
)
