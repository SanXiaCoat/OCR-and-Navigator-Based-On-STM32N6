package com.sanxia.stm32ARmap;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.util.Log;

import org.json.JSONObject;

import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.List;
import java.util.Queue;
import java.util.UUID;

public class MacBleJsonSender {
    private static final String TAG = "AtkBleJsonSender";
    public static final UUID SERVICE_UUID =
            UUID.fromString("0000fff0-0000-1000-8000-00805f9b34fb");
    public static final UUID CHARACTERISTIC_UUID =
            UUID.fromString("0000fff2-0000-1000-8000-00805f9b34fb");

    private static final long SCAN_TIMEOUT_MS = 10000L;
    private static final int REQUESTED_MTU = 247;
    private static final long CHUNK_INTERVAL_NO_RESPONSE_MS = 15L;
    private static final long CHUNK_RETRY_MS = 50L;

    public interface Callback {
        void onConnected(String deviceName);

        void onDisconnected();

        void onError(String message, Exception exception);
    }

    private final Context context;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private BluetoothGatt bluetoothGatt;
    private BluetoothGattCharacteristic writeCharacteristic;
    private BluetoothLeScanner scanner;
    private ScanCallback scanCallback;
    private Callback callback;
    private int serviceDiscoveryRetryCount = 0;
    private final Queue<byte[]> writeQueue = new ArrayDeque<>();
    private boolean writeQueueActive = false;
    private int mtuPayloadSize = 20;
    private boolean waitingWriteCallback = false;

    public MacBleJsonSender(Context context) {
        this.context = context.getApplicationContext();
    }

    public boolean isBluetoothAvailable() {
        BluetoothManager manager = (BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE);
        return manager != null && manager.getAdapter() != null;
    }

    public boolean hasBlePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)
                    == PackageManager.PERMISSION_GRANTED
                    && context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                    == PackageManager.PERMISSION_GRANTED;
        }
        return context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                == PackageManager.PERMISSION_GRANTED;
    }

    @SuppressLint("MissingPermission")
    public void scanAndConnect(Callback callback) {
        this.callback = callback;
        if (!isBluetoothAvailable()) {
            notifyError("当前设备不支持蓝牙", null);
            return;
        }
        if (!hasBlePermissions()) {
            notifyError("缺少 BLE 扫描或连接权限", null);
            return;
        }

        BluetoothAdapter adapter = ((BluetoothManager) context.getSystemService(Context.BLUETOOTH_SERVICE)).getAdapter();
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            notifyError("BLE 扫描不可用，请确认蓝牙已开启", null);
            return;
        }

        close();
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();

        scanCallback = new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult result) {
                if (!isAtkBleReceiver(result)) {
                    return;
                }

                stopScan();
                BluetoothDevice device = result.getDevice();
                bluetoothGatt = connectGatt(device);
            }

            @Override
            public void onScanFailed(int errorCode) {
                notifyError("BLE 扫描失败：" + errorCode, null);
            }
        };

        scanner.startScan(null, settings, scanCallback);
        mainHandler.postDelayed(() -> {
            if (scanCallback != null && bluetoothGatt == null) {
                stopScan();
                notifyError("未找到 ATK-BLE04，请确认模块已上电并正在广播", null);
            }
        }, SCAN_TIMEOUT_MS);
    }

    @SuppressLint("MissingPermission")
    private BluetoothGatt connectGatt(BluetoothDevice device) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
        }
        return device.connectGatt(context, false, gattCallback);
    }

    @SuppressLint("MissingPermission")
    private boolean isAtkBleReceiver(ScanResult result) {
        ScanRecord record = result.getScanRecord();
        if (record != null) {
            List<ParcelUuid> serviceUuids = record.getServiceUuids();
            if (serviceUuids != null && serviceUuids.contains(new ParcelUuid(SERVICE_UUID))) {
                return true;
            }

            String advertisedName = record.getDeviceName();
            if (advertisedName != null && advertisedName.toUpperCase().contains("ATK")) {
                return true;
            }
        }

        BluetoothDevice device = result.getDevice();
        String deviceName = device == null ? null : device.getName();
        return deviceName != null && deviceName.toUpperCase().contains("ATK");
    }

    @SuppressLint("MissingPermission")
    public void sendJson(JSONObject jsonObject, Callback callback) {
        if (jsonObject == null || bluetoothGatt == null || writeCharacteristic == null || !hasBlePermissions()) {
            return;
        }

        this.callback = callback;
        byte[] payload = (jsonObject.toString() + "\n").getBytes(StandardCharsets.UTF_8);
        enqueuePayload(payload);
    }

    public void sendLine(String line, Callback callback) {
        if (line == null || bluetoothGatt == null || writeCharacteristic == null || !hasBlePermissions()) {
            return;
        }

        this.callback = callback;
        byte[] payload = (line + "\n").getBytes(StandardCharsets.UTF_8);
        enqueuePayload(payload);
    }

    @SuppressLint("MissingPermission")
    public void close() {
        stopScan();
        writeCharacteristic = null;
        writeQueue.clear();
        writeQueueActive = false;
        waitingWriteCallback = false;
        mtuPayloadSize = 20;
        if (bluetoothGatt != null) {
            bluetoothGatt.disconnect();
            bluetoothGatt.close();
            bluetoothGatt = null;
        }
    }

    @SuppressLint("MissingPermission")
    private void stopScan() {
        if (scanner != null && scanCallback != null && hasBlePermissions()) {
            scanner.stopScan(scanCallback);
        }
        scanCallback = null;
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                notifyError("ATK-BLE04 连接失败：" + status, null);
                return;
            }

            if (newState == BluetoothProfile.STATE_CONNECTED) {
                serviceDiscoveryRetryCount = 0;
                if (!gatt.requestMtu(REQUESTED_MTU)) {
                    mainHandler.postDelayed(gatt::discoverServices, 300L);
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                writeCharacteristic = null;
                writeQueue.clear();
                writeQueueActive = false;
                waitingWriteCallback = false;
                mtuPayloadSize = 20;
                if (callback != null) {
                    callback.onDisconnected();
                }
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                mtuPayloadSize = Math.max(20, mtu - 3);
                Log.d(TAG, "MTU=" + mtu + ", payload=" + mtuPayloadSize);
            }
            if (writeCharacteristic == null) {
                gatt.discoverServices();
            }
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                notifyError("ATK-BLE04 服务发现失败：" + status, null);
                return;
            }

            writeCharacteristic = findWriteCharacteristic(gatt);
            if (writeCharacteristic == null) {
                retryDiscoverServicesOrReport(gatt);
                return;
            }

            gatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);

            if (callback != null) {
                callback.onConnected(getDeviceName(gatt.getDevice()));
            }
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                notifyError("ATK-BLE04 写入失败：" + status, null);
                waitingWriteCallback = false;
                return;
            }
            if (waitingWriteCallback) {
                waitingWriteCallback = false;
                mainHandler.post(MacBleJsonSender.this::writeNextQueuedChunk);
            }
        }
    };

    private void enqueuePayload(byte[] payload) {
        mainHandler.post(() -> {
            writeQueue.clear();
            writeQueueActive = false;
            waitingWriteCallback = false;

            int chunkSize = mtuPayloadSize;
            for (int offset = 0; offset < payload.length; offset += chunkSize) {
                int end = Math.min(offset + chunkSize, payload.length);
                writeQueue.offer(Arrays.copyOfRange(payload, offset, end));
            }
            writeNextQueuedChunk();
        });
    }

    @SuppressLint("MissingPermission")
    private void writeNextQueuedChunk() {
        if (waitingWriteCallback) {
            return;
        }
        if (bluetoothGatt == null || writeCharacteristic == null || !hasBlePermissions()) {
            writeQueueActive = false;
            return;
        }

        byte[] chunk = writeQueue.peek();
        if (chunk == null) {
            writeQueueActive = false;
            return;
        }

        writeQueueActive = true;
        int writeType = resolveWriteType(writeCharacteristic);
        writeCharacteristic.setWriteType(writeType);
        writeCharacteristic.setValue(chunk);
        boolean accepted = bluetoothGatt.writeCharacteristic(writeCharacteristic);
        if (!accepted) {
            Log.d(TAG, "BLE busy, retrying chunk later");
            mainHandler.postDelayed(this::writeNextQueuedChunk, CHUNK_RETRY_MS);
            return;
        }

        writeQueue.poll();
        if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) {
            mainHandler.postDelayed(this::writeNextQueuedChunk, CHUNK_INTERVAL_NO_RESPONSE_MS);
        } else {
            waitingWriteCallback = true;
        }
    }

    private int resolveWriteType(BluetoothGattCharacteristic characteristic) {
        int properties = characteristic.getProperties();
        if ((properties & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0) {
            return BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE;
        }
        return BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
    }

    @SuppressLint("MissingPermission")
    private void retryDiscoverServicesOrReport(BluetoothGatt gatt) {
        if (serviceDiscoveryRetryCount < 2) {
            serviceDiscoveryRetryCount++;
            refreshGattCache(gatt);
            mainHandler.postDelayed(gatt::discoverServices, 1200L);
            return;
        }

        notifyError("ATK-BLE04 缺少 FFF2 写入特征值，发现：" + buildDiscoveredServicesSummary(gatt), null);
    }

    private BluetoothGattCharacteristic findWriteCharacteristic(BluetoothGatt gatt) {
        BluetoothGattService service = gatt.getService(SERVICE_UUID);
        if (service == null) {
            logDiscoveredServices(gatt);
            return null;
        }

        BluetoothGattCharacteristic characteristic = service.getCharacteristic(CHARACTERISTIC_UUID);
        if (isWritable(characteristic)) {
            return characteristic;
        }

        for (BluetoothGattCharacteristic candidate : service.getCharacteristics()) {
            if (isWritable(candidate)) {
                return candidate;
            }
        }

        BluetoothGattCharacteristic anyWritable = findAnyWritableCharacteristic(gatt);
        if (anyWritable != null) {
            return anyWritable;
        }

        logDiscoveredServices(gatt);
        return null;
    }

    private BluetoothGattCharacteristic findAnyWritableCharacteristic(BluetoothGatt gatt) {
        for (BluetoothGattService service : gatt.getServices()) {
            for (BluetoothGattCharacteristic characteristic : service.getCharacteristics()) {
                if (isWritable(characteristic)) {
                    return characteristic;
                }
            }
        }
        return null;
    }

    private boolean isWritable(BluetoothGattCharacteristic characteristic) {
        if (characteristic == null) {
            return false;
        }

        int properties = characteristic.getProperties();
        return (properties & BluetoothGattCharacteristic.PROPERTY_WRITE) != 0
                || (properties & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0;
    }

    private void logDiscoveredServices(BluetoothGatt gatt) {
        List<BluetoothGattService> services = gatt.getServices();
        for (BluetoothGattService service : services) {
            Log.d(TAG, "BLE service: " + service.getUuid());
            for (BluetoothGattCharacteristic characteristic : service.getCharacteristics()) {
                Log.d(TAG, "  characteristic: " + characteristic.getUuid()
                        + ", properties=" + characteristic.getProperties());
            }
        }
    }

    private String buildDiscoveredServicesSummary(BluetoothGatt gatt) {
        StringBuilder builder = new StringBuilder();
        List<BluetoothGattService> services = gatt.getServices();
        if (services.isEmpty()) {
            return "0 个服务";
        }

        builder.append(services.size()).append(" 个服务 ");
        int serviceCount = 0;
        for (BluetoothGattService service : services) {
            if (serviceCount >= 3) {
                builder.append("...");
                break;
            }
            builder.append(shortUuid(service.getUuid())).append("[");
            List<BluetoothGattCharacteristic> characteristics = service.getCharacteristics();
            if (characteristics.isEmpty()) {
                builder.append("无特征值");
            } else {
                int charCount = 0;
                for (BluetoothGattCharacteristic characteristic : characteristics) {
                    if (charCount >= 3) {
                        builder.append("...");
                        break;
                    }
                    if (charCount > 0) {
                        builder.append(",");
                    }
                    builder.append(shortUuid(characteristic.getUuid()))
                            .append(":")
                            .append(characteristic.getProperties());
                    charCount++;
                }
            }
            builder.append("] ");
            serviceCount++;
        }
        return builder.toString();
    }

    private String shortUuid(UUID uuid) {
        String value = uuid.toString();
        return value.length() > 8 ? value.substring(0, 8) : value;
    }

    private void refreshGattCache(BluetoothGatt gatt) {
        try {
            Method refresh = gatt.getClass().getMethod("refresh");
            refresh.invoke(gatt);
        } catch (Exception e) {
            Log.d(TAG, "Unable to refresh GATT cache", e);
        }
    }

    @SuppressLint("MissingPermission")
    private String getDeviceName(BluetoothDevice device) {
        if (device == null || !hasBlePermissions()) {
            return "ATK-BLE04";
        }
        String name = device.getName();
        return name == null || name.trim().isEmpty() ? "ATK-BLE04" : name;
    }

    private void notifyError(String message, Exception exception) {
        if (callback != null) {
            callback.onError(message, exception);
        }
    }
}
