import CoreBluetooth
import Foundation

private let serviceUUID = CBUUID(string: "7A08F4C2-7F8E-4C72-9A20-8B6B7D4D1A10")
private let characteristicUUID = CBUUID(string: "7A08F4C3-7F8E-4C72-9A20-8B6B7D4D1A10")
private let logFileURL = FileManager.default.homeDirectoryForCurrentUser
    .appendingPathComponent("Desktop")
    .appendingPathComponent("NaviJsonMac.log")

private func log(_ message: String) {
    let line = "[\(Date())] \(message)\n"
    print(message)
    if let data = line.data(using: .utf8) {
        if FileManager.default.fileExists(atPath: logFileURL.path),
           let handle = try? FileHandle(forWritingTo: logFileURL) {
            handle.seekToEndOfFile()
            handle.write(data)
            try? handle.close()
        } else {
            try? data.write(to: logFileURL)
        }
    }
}

final class BleNaviReceiver: NSObject, CBPeripheralManagerDelegate {
    private var peripheralManager: CBPeripheralManager!
    private var naviService: CBMutableService?
    private var naviCharacteristic: CBMutableCharacteristic?
    private var receiveBuffer = Data()

    override init() {
        super.init()
        log("Starting NaviJsonMac BLE receiver...")
        log("Log file: \(logFileURL.path)")
        peripheralManager = CBPeripheralManager(delegate: self, queue: nil)
    }

    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        switch peripheral.state {
        case .poweredOn:
            log("Bluetooth is powered on. Adding BLE service...")
            startAdvertising()
        case .poweredOff:
            log("Bluetooth is powered off. Turn it on in macOS Settings.")
        case .unauthorized:
            log("Bluetooth permission is not granted for NaviJsonMac.")
        default:
            log("Bluetooth state: \(peripheral.state.rawValue)")
        }
    }

    private func startAdvertising() {
        peripheralManager.stopAdvertising()
        peripheralManager.removeAllServices()

        naviCharacteristic = CBMutableCharacteristic(
            type: characteristicUUID,
            properties: [.write],
            value: nil,
            permissions: [.writeable]
        )
        naviService = CBMutableService(type: serviceUUID, primary: true)
        if let naviCharacteristic {
            naviService?.characteristics = [naviCharacteristic]
        }

        if let naviService {
            peripheralManager.add(naviService)
        }
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didAdd service: CBService, error: Error?) {
        if let error {
            log("Failed to add BLE service: \(error.localizedDescription)")
            return
        }

        peripheralManager.startAdvertising([
            CBAdvertisementDataLocalNameKey: "NaviJsonMac",
            CBAdvertisementDataServiceUUIDsKey: [serviceUUID]
        ])
        log("NaviJsonMac is advertising. Tap \"连接 Mac\" in the Android app.")
    }

    func peripheralManagerDidStartAdvertising(_ peripheral: CBPeripheralManager, error: Error?) {
        if let error {
            log("Failed to advertise: \(error.localizedDescription)")
        } else {
            log("BLE advertising started successfully.")
        }
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        for request in requests {
            guard request.characteristic.uuid == characteristicUUID, let value = request.value else {
                peripheral.respond(to: request, withResult: .requestNotSupported)
                continue
            }

            log("Received \(value.count) bytes")
            receiveBuffer.append(value)
            printCompleteJsonLines()
            peripheral.respond(to: request, withResult: .success)
        }
    }

    private func printCompleteJsonLines() {
        while let newlineRange = receiveBuffer.firstRange(of: Data([0x0A])) {
            let lineData = receiveBuffer[..<newlineRange.lowerBound]
            receiveBuffer.removeSubrange(..<newlineRange.upperBound)

            guard !lineData.isEmpty else {
                continue
            }

            if let line = String(data: lineData, encoding: .utf8) {
                log(line)
            } else {
                log("Received non-UTF8 payload: \(lineData as NSData)")
            }
        }
    }
}

let receiver = BleNaviReceiver()
RunLoop.main.run()
