package com.sanxia.stm32ARmap;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;

import org.json.JSONObject;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class BluetoothJsonSender {
    private static final UUID SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    public interface Callback {
        void onConnected(BluetoothDevice device);

        void onDisconnected();

        void onError(String message, Exception exception);
    }

    private final Context context;
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private BluetoothSocket socket;
    private OutputStream outputStream;

    public BluetoothJsonSender(Context context) {
        this.context = context.getApplicationContext();
    }

    public boolean isBluetoothAvailable() {
        return BluetoothAdapter.getDefaultAdapter() != null;
    }

    public boolean hasConnectPermission() {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S
                || context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                == PackageManager.PERMISSION_GRANTED;
    }

    @SuppressLint("MissingPermission")
    public List<BluetoothDevice> getPairedDevices() {
        List<BluetoothDevice> devices = new ArrayList<>();
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null || !hasConnectPermission()) {
            return devices;
        }

        Set<BluetoothDevice> bondedDevices = adapter.getBondedDevices();
        if (bondedDevices != null) {
            devices.addAll(bondedDevices);
        }
        return devices;
    }

    @SuppressLint("MissingPermission")
    public String getDisplayName(BluetoothDevice device) {
        if (device == null) {
            return "未知设备";
        }
        String name = hasConnectPermission() ? device.getName() : null;
        if (name == null || name.trim().isEmpty()) {
            name = "未知设备";
        }
        return name + "\n" + device.getAddress();
    }

    public void connect(BluetoothDevice device, Callback callback) {
        executor.execute(() -> {
            BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
            if (adapter == null) {
                notifyError(callback, "设备不支持蓝牙", null);
                return;
            }
            if (!hasConnectPermission()) {
                notifyError(callback, "缺少蓝牙连接权限", null);
                return;
            }

            try {
                closeConnectedSocket();
                socket = device.createRfcommSocketToServiceRecord(SPP_UUID);
                socket.connect();
                outputStream = socket.getOutputStream();
                if (callback != null) {
                    callback.onConnected(device);
                }
            } catch (IOException | SecurityException e) {
                closeConnectedSocket();
                notifyError(callback, "蓝牙连接失败", e);
            }
        });
    }

    public void sendJson(JSONObject jsonObject, Callback callback) {
        if (jsonObject == null) {
            return;
        }

        executor.execute(() -> {
            if (outputStream == null) {
                return;
            }

            try {
                String line = jsonObject.toString() + "\n";
                outputStream.write(line.getBytes(StandardCharsets.UTF_8));
                outputStream.flush();
            } catch (IOException e) {
                closeConnectedSocket();
                if (callback != null) {
                    callback.onDisconnected();
                }
                notifyError(callback, "蓝牙发送失败", e);
            }
        });
    }

    public void close() {
        executor.execute(this::closeConnectedSocket);
        executor.shutdown();
    }

    private void closeConnectedSocket() {
        try {
            if (outputStream != null) {
                outputStream.close();
            }
        } catch (IOException ignored) {
        } finally {
            outputStream = null;
        }

        try {
            if (socket != null) {
                socket.close();
            }
        } catch (IOException ignored) {
        } finally {
            socket = null;
        }
    }

    private void notifyError(Callback callback, String message, Exception exception) {
        if (callback != null) {
            callback.onError(message, exception);
        }
    }
}
