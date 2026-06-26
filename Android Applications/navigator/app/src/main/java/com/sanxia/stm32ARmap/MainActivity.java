package com.sanxia.stm32ARmap;

import android.Manifest;
import android.bluetooth.BluetoothDevice;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.amap.api.maps.model.LatLng;
import com.amap.api.navi.AMapNavi;
import com.amap.api.navi.AMapNaviListener;
import com.amap.api.navi.AMapNaviView;
import com.amap.api.navi.AMapNaviViewListener;
import com.amap.api.navi.AmapPageType;
import com.amap.api.navi.AmapNaviType;
import com.amap.api.navi.AMapNaviViewOptions;
import com.amap.api.navi.enums.IconType;
import com.amap.api.navi.enums.NaviType;
import com.amap.api.navi.model.AMapCalcRouteResult;
import com.amap.api.navi.model.AMapLaneInfo;
import com.amap.api.navi.model.AMapModelCross;
import com.amap.api.navi.model.AMapNaviCameraInfo;
import com.amap.api.navi.model.AMapNaviCross;
import com.amap.api.navi.model.AMapNaviLocation;
import com.amap.api.navi.model.AMapNaviRouteNotifyData;
import com.amap.api.navi.model.AMapNaviTrafficFacilityInfo;
import com.amap.api.navi.model.AMapServiceAreaInfo;
import com.amap.api.navi.model.AimLessModeCongestionInfo;
import com.amap.api.navi.model.AimLessModeStat;
import com.amap.api.navi.model.NaviInfo;
import com.amap.api.navi.model.NaviLatLng;
import com.amap.api.navi.model.NaviPoi;
import com.amap.api.navi.NaviSetting;
import com.amap.api.services.core.AMapException;
import com.amap.api.services.core.PoiItem;
import com.amap.api.services.poisearch.PoiResult;
import com.amap.api.services.poisearch.PoiSearch;

import org.json.JSONException;
import org.json.JSONObject;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends AppCompatActivity implements AMapNaviListener, AMapNaviViewListener, PoiSearch.OnPoiSearchListener {

    private static final int PERMISSION_REQUEST_CODE = 100;
    private static final int BLUETOOTH_PERMISSION_REQUEST_CODE = 101;
    private static final String TAG = "RealTimeNavi";
    private static final long NAVI_JSON_MIN_INTERVAL_MS = 1000L;

    private AMapNavi mAMapNavi;
    private AMapNaviView mAMapNaviView;

    // UI控件
    private EditText etSearch;
    private RecyclerView rvResult;
    private Button btnStartNavi;
    private Button btnStartRideNavi;
    private Button btnConnectBluetooth;
    private Button btnConnectMacBle;
    private Button btnSendAtkTest;
    private ImageView ivNavAction;
    private TextView tvNavAction;
    private SearchResultAdapter searchAdapter;
    private final List<PoiItem> poiList = new ArrayList<>();
    private PoiItem selectedPoi = null;
    private BluetoothJsonSender bluetoothJsonSender;
    private BluetoothJsonSender.Callback bluetoothCallback;
    private MacBleJsonSender macBleJsonSender;
    private MacBleJsonSender.Callback macBleCallback;
    private String latestNavigationText = "";
    private long lastNaviJsonSendTime = 0L;
    private String currentRouteType = "步行";
    private NavAction latestNavAction = NavAction.unknown();
    private boolean atkBleConnected = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 高德隐私合规（必须放在最前面）
        NaviSetting.updatePrivacyShow(this, true, true);
        NaviSetting.updatePrivacyAgree(this, true);

        setContentView(R.layout.activity_main);

        // 初始化导航视图
        mAMapNaviView = findViewById(R.id.navi_view);
        mAMapNaviView.onCreate(savedInstanceState);
        mAMapNaviView.setAMapNaviViewListener(this);

        // 初始化蓝牙发送器
        initBluetoothSender();
        // 初始化UI
        initView();
        // 初始化导航
        initNavi();
        // 申请权限
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            checkAndRequestNavigationPermissions();
        }
    }

    private void initBluetoothSender() {
        bluetoothJsonSender = new BluetoothJsonSender(this);
        bluetoothCallback = new BluetoothJsonSender.Callback() {
            @Override
            public void onConnected(BluetoothDevice device) {
                runOnUiThread(() -> Toast.makeText(
                        MainActivity.this,
                        "蓝牙已连接：" + bluetoothJsonSender.getDisplayName(device).split("\n")[0],
                        Toast.LENGTH_SHORT
                ).show());
            }

            @Override
            public void onDisconnected() {
                runOnUiThread(() -> Toast.makeText(
                        MainActivity.this,
                        "蓝牙连接已断开",
                        Toast.LENGTH_SHORT
                ).show());
            }

            @Override
            public void onError(String message, Exception exception) {
                if (exception != null) {
                    Log.e(TAG, message, exception);
                }
                runOnUiThread(() -> Toast.makeText(MainActivity.this, message, Toast.LENGTH_SHORT).show());
            }
        };

        macBleJsonSender = new MacBleJsonSender(this);
        macBleCallback = new MacBleJsonSender.Callback() {
            @Override
            public void onConnected(String deviceName) {
                atkBleConnected = true;
                sendMacConnectionTestJson();
                runOnUiThread(() -> Toast.makeText(
                        MainActivity.this,
                        "ATK-BLE04 已连接：" + deviceName + "，已发送测试 JSON",
                        Toast.LENGTH_LONG
                ).show());
            }

            @Override
            public void onDisconnected() {
                atkBleConnected = false;
                runOnUiThread(() -> Toast.makeText(
                        MainActivity.this,
                        "ATK-BLE04 连接已断开",
                        Toast.LENGTH_SHORT
                ).show());
            }

            @Override
            public void onError(String message, Exception exception) {
                if (exception != null) {
                    Log.e(TAG, message, exception);
                }
                runOnUiThread(() -> Toast.makeText(MainActivity.this, message, Toast.LENGTH_SHORT).show());
            }
        };
    }

    private void initView() {
        etSearch = findViewById(R.id.et_search);
        rvResult = findViewById(R.id.rv_result);
        btnStartNavi = findViewById(R.id.btn_start_navi);
        btnStartRideNavi = findViewById(R.id.btn_start_ride_navi);
        btnConnectBluetooth = findViewById(R.id.btn_connect_bluetooth);
        btnConnectMacBle = findViewById(R.id.btn_connect_mac_ble);
        btnSendAtkTest = findViewById(R.id.btn_send_atk_test);
        ivNavAction = findViewById(R.id.iv_nav_action);
        tvNavAction = findViewById(R.id.tv_nav_action);

        // 初始化搜索列表
        rvResult.setLayoutManager(new LinearLayoutManager(this));
        searchAdapter = new SearchResultAdapter(poiList, item -> {
            // 选中目的地后的逻辑
            selectedPoi = item;
            etSearch.setText(item.getTitle()); // 回填地址到输入框
            etSearch.setSelection(item.getTitle().length()); // 光标移到末尾
            hideKeyboard(); // 隐藏键盘
            rvResult.setVisibility(android.view.View.GONE); // 隐藏下拉栏
            Toast.makeText(this, "已选择：" + item.getTitle(), Toast.LENGTH_SHORT).show();
        });
        rvResult.setAdapter(searchAdapter);

        // 实时搜索监听
        etSearch.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                String keyword = s.toString().trim();
                if (keyword.length() > 0) {
                    startSearch(keyword); // 有内容时发起搜索
                } else {
                    // 无内容时清空列表+隐藏下拉栏
                    poiList.clear();
                    searchAdapter.notifyDataSetChanged();
                    rvResult.setVisibility(android.view.View.GONE);
                    selectedPoi = null;
                }
            }

            @Override
            public void afterTextChanged(Editable s) {}
        });

        btnStartNavi.setOnClickListener(v -> {
            sendAtkDebugLine("NAV_BUTTON,WALK");
            if (selectedPoi == null) {
                sendAtkDebugLine("NAV_NO_POI,WALK");
                Toast.makeText(this, "请先选择目的地", Toast.LENGTH_SHORT).show();
                return;
            }
            hideKeyboard();
            sendAtkDebugLine("NAV_CLICK,WALK");
            calculateWalkRoute(selectedPoi);
        });

        btnStartRideNavi.setOnClickListener(v -> {
            sendAtkDebugLine("NAV_BUTTON,RIDE");
            if (selectedPoi == null) {
                sendAtkDebugLine("NAV_NO_POI,RIDE");
                Toast.makeText(this, "请先选择目的地", Toast.LENGTH_SHORT).show();
                return;
            }
            hideKeyboard();
            sendAtkDebugLine("NAV_CLICK,RIDE");
            calculateRideRoute(selectedPoi);
        });

        btnConnectBluetooth.setOnClickListener(v -> showBluetoothDevicePicker());
        btnConnectMacBle.setOnClickListener(v -> connectMacBleReceiver());
        btnSendAtkTest.setOnClickListener(v -> {
            sendAtkDebugLine("ATK_MANUAL_TEST_V2");
            Toast.makeText(this, "已发送 ATK 手动测试", Toast.LENGTH_SHORT).show();
        });
    }

    // ======================
    // 搜索POI（已修复异常）
    // ======================
    private void startSearch(String keyword) {
        PoiSearch.Query query = new PoiSearch.Query(keyword, "", "全国");
        query.setPageSize(10); // 最多显示10条结果

        try {
            PoiSearch poiSearch = new PoiSearch(this, query);
            poiSearch.setOnPoiSearchListener(this);
            poiSearch.searchPOIAsyn();
        } catch (AMapException e) {
            e.printStackTrace();
            Toast.makeText(this, "搜索失败", Toast.LENGTH_SHORT).show();
        }
    }

    // 搜索结果回调
    @Override
    public void onPoiSearched(PoiResult poiResult, int code) {
        if (code == AMapException.CODE_AMAP_SUCCESS && poiResult != null && !poiResult.getPois().isEmpty()) {
            poiList.clear();
            poiList.addAll(poiResult.getPois());
            searchAdapter.notifyDataSetChanged();
            rvResult.setVisibility(android.view.View.VISIBLE); // 有结果时显示下拉栏
        } else {
            // 无结果时隐藏下拉栏
            poiList.clear();
            searchAdapter.notifyDataSetChanged();
            rvResult.setVisibility(android.view.View.GONE);
        }
    }

    @Override
    public void onPoiItemSearched(PoiItem poiItem, int i) {}

    // ======================
    // 用选中的POI规划步行/骑行路线
    // ======================
    private void calculateWalkRoute(PoiItem endPoiItem) {
        calculateTravelRoute(endPoiItem, true);
    }

    private void calculateRideRoute(PoiItem endPoiItem) {
        calculateTravelRoute(endPoiItem, false);
    }

    private void calculateTravelRoute(PoiItem endPoiItem, boolean isWalk) {
        if (mAMapNavi == null || endPoiItem == null || endPoiItem.getLatLonPoint() == null) {
            Toast.makeText(this, "导航暂不可用", Toast.LENGTH_SHORT).show();
            return;
        }

        currentRouteType = isWalk ? "步行" : "骑行";
        configureNaviViewForTravelType(isWalk);
        NaviLatLng endLatLng = new NaviLatLng(
                endPoiItem.getLatLonPoint().getLatitude(),
                endPoiItem.getLatLonPoint().getLongitude()
        );
        sendRouteEventJson("route_request", "start", endPoiItem.getTitle(), 0);

        boolean isSuccess = isWalk
                ? mAMapNavi.calculateWalkRoute(endLatLng)
                : mAMapNavi.calculateRideRoute(endLatLng);
        if (isSuccess) {
            Toast.makeText(this, "正在规划" + currentRouteType + "路线...", Toast.LENGTH_SHORT).show();
        } else {
            Toast.makeText(this, currentRouteType + "路线规划发起失败", Toast.LENGTH_SHORT).show();
        }
    }

    private void configureNaviViewForTravelType(boolean isWalk) {
        if (mAMapNaviView == null) {
            return;
        }

        AMapNaviViewOptions options = new AMapNaviViewOptions();
        options.setAMapNaviType(isWalk ? AmapNaviType.WALK : AmapNaviType.RIDE);
        options.setAutoDrawRoute(true);
        options.setAutoDisplayOverview(true);
        options.setLayoutVisible(true);
        mAMapNaviView.setViewOptions(options);
        mAMapNaviView.setRouteMarkerVisible(true, true, true);
    }

    // ======================
    // 导航初始化
    // ======================
    private void initNavi() {
        try {
            mAMapNavi = AMapNavi.getInstance(getApplicationContext());
            mAMapNavi.addAMapNaviListener(this);
            mAMapNavi.setUseInnerVoice(true); // 开启内置语音播报
        } catch (Exception e) {
            e.printStackTrace();
            Toast.makeText(this, "导航初始化失败", Toast.LENGTH_SHORT).show();
        }
    }

    // ======================
    // 路线规划成功 → 自动启动实时导航
    // ======================
    @Override
    public void onCalculateRouteSuccess(AMapCalcRouteResult routeResult) {
        runOnUiThread(() -> {
            if (mAMapNavi != null) {
                boolean isStartSuccess = mAMapNavi.startNavi(NaviType.GPS);
                if (isStartSuccess) {
                    sendRouteEventJson("route_success", "navi_started", null, 0);
                    Toast.makeText(this, "开始" + currentRouteType + "导航", Toast.LENGTH_SHORT).show();
                } else {
                    sendRouteEventJson("route_success", "start_navi_failed", null, 0);
                    Toast.makeText(this, "导航启动失败", Toast.LENGTH_SHORT).show();
                }
            }
        });
    }

    @Override
    public void onCalculateRouteFailure(AMapCalcRouteResult routeResult) {
        runOnUiThread(() -> {
            sendRouteEventJson("route_failure", "calculate_failed", null, routeResult.getErrorCode());
            Toast.makeText(this, currentRouteType + "路线规划失败，错误码：" + routeResult.getErrorCode(), Toast.LENGTH_SHORT).show();
        });
    }

    // ======================
    // 隐藏键盘工具方法
    // ======================
    private void hideKeyboard() {
        try {
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null && getCurrentFocus() != null) {
                imm.hideSoftInputFromWindow(getCurrentFocus().getWindowToken(), 0);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    private void showBluetoothDevicePicker() {
        if (bluetoothJsonSender == null || !bluetoothJsonSender.isBluetoothAvailable()) {
            Toast.makeText(this, "当前设备不支持蓝牙", Toast.LENGTH_SHORT).show();
            return;
        }

        if (!bluetoothJsonSender.hasConnectPermission()) {
            checkAndRequestBluetoothPermissions();
            Toast.makeText(this, "请先授予蓝牙连接权限", Toast.LENGTH_SHORT).show();
            return;
        }

        List<BluetoothDevice> devices = bluetoothJsonSender.getPairedDevices();
        if (devices.isEmpty()) {
            Toast.makeText(this, "没有已配对的蓝牙设备，请先到系统设置中配对", Toast.LENGTH_LONG).show();
            return;
        }

        String[] labels = new String[devices.size()];
        for (int i = 0; i < devices.size(); i++) {
            labels[i] = bluetoothJsonSender.getDisplayName(devices.get(i));
        }

        new AlertDialog.Builder(this)
                .setTitle("选择蓝牙设备")
                .setItems(labels, (dialog, which) -> {
                    Toast.makeText(this, "正在连接蓝牙...", Toast.LENGTH_SHORT).show();
                    bluetoothJsonSender.connect(devices.get(which), bluetoothCallback);
                })
                .show();
    }

    private void connectMacBleReceiver() {
        if (macBleJsonSender == null || !macBleJsonSender.isBluetoothAvailable()) {
            Toast.makeText(this, "当前设备不支持 BLE", Toast.LENGTH_SHORT).show();
            return;
        }

        if (!macBleJsonSender.hasBlePermissions()) {
            checkAndRequestBluetoothPermissions();
            Toast.makeText(this, "请先授予 BLE 扫描和连接权限", Toast.LENGTH_SHORT).show();
            return;
        }

        Toast.makeText(this, "正在扫描 ATK-BLE04...", Toast.LENGTH_SHORT).show();
        macBleJsonSender.scanAndConnect(macBleCallback);
    }

    private void sendMacConnectionTestJson() {
        macBleJsonSender.sendLine("ATK_CONNECTED_V2", macBleCallback);
    }

    private void sendAtkDebugLine(String line) {
        if (atkBleConnected && macBleJsonSender != null) {
            macBleJsonSender.sendLine(line, macBleCallback);
        }
    }

    private void sendJsonToConnectedDevices(JSONObject jsonObject) {
        if (bluetoothJsonSender != null) {
            bluetoothJsonSender.sendJson(jsonObject, bluetoothCallback);
        }
        if (macBleJsonSender != null) {
            macBleJsonSender.sendJson(jsonObject, macBleCallback);
        }
    }

    private void sendRouteEventJson(String type, String status, String destination, int errorCode) {
        try {
            JSONObject jsonObject = new JSONObject();
            jsonObject.put("type", type);
            jsonObject.put("routeType", currentRouteType);
            jsonObject.put("status", status);
            jsonObject.put("timestamp", System.currentTimeMillis());
            if (destination != null && !destination.isEmpty()) {
                jsonObject.put("destination", destination);
            }
            if (errorCode != 0) {
                jsonObject.put("errorCode", errorCode);
            }
            sendJsonToConnectedDevices(jsonObject);
        } catch (JSONException e) {
            Log.e(TAG, "路线事件 JSON 生成失败", e);
        }
    }

    private void sendNaviInfoJson(NaviInfo naviInfo) {
        if (naviInfo == null) {
            return;
        }

        long now = System.currentTimeMillis();
        if (now - lastNaviJsonSendTime < NAVI_JSON_MIN_INTERVAL_MS) {
            return;
        }
        lastNaviJsonSendTime = now;

        try {
            Object nextActionDistance = readValue(naviInfo, "getCurStepRetainDistance");
            Object iconTypeValue = readValue(naviInfo, "getIconType");
            NavAction navAction = resolveNavAction(iconTypeValue, latestNavigationText, nextActionDistance);
            latestNavAction = navAction;
            updateNavActionUi(navAction);

            JSONObject jsonObject = new JSONObject();
            jsonObject.put("type", "navi_update");
            jsonObject.put("timestamp", now);
            putIfPresent(jsonObject, "curRoad", readValue(naviInfo,
                    "getCurrentRoadName", "getCurRoadName"));
            jsonObject.put("action", navAction.action);
            jsonObject.put("actionText", navAction.actionText);
            putIfPresent(jsonObject, "nextActionDistance", nextActionDistance);
            putIfPresent(jsonObject, "nextRoad", readValue(naviInfo, "getNextRoadName"));
            putIfPresent(jsonObject, "remainTime", readValue(naviInfo,
                    "getPathRetainTime", "getRouteRemainTime"));
            sendJsonToConnectedDevices(jsonObject);
        } catch (JSONException e) {
            Log.e(TAG, "导航信息 JSON 生成失败", e);
        }
    }

    private NavAction resolveNavAction(Object iconTypeValue, String navigationText, Object distanceValue) {
        int iconType = toInt(iconTypeValue, IconType.NONE);
        NavAction baseAction = actionFromText(navigationText);
        if (!"unknown".equals(baseAction.action)) {
            return baseAction.withText(buildActionText(baseAction.label, navigationText, distanceValue));
        }

        NavAction iconAction = actionFromIconType(iconType);
        return iconAction.withText(buildActionText(iconAction.label, navigationText, distanceValue));
    }

    private NavAction actionFromText(String text) {
        String value = text == null ? "" : text;
        if (value.contains("上楼梯") || value.contains("上台阶") || value.contains("上扶梯")) {
            return new NavAction("stairs_up", "ic_nav_stairs_up", R.drawable.ic_nav_stairs_up, "上楼梯");
        }
        if (value.contains("下楼梯") || value.contains("下台阶") || value.contains("下扶梯")) {
            return new NavAction("stairs_down", "ic_nav_stairs_down", R.drawable.ic_nav_stairs_down, "下楼梯");
        }
        if (value.contains("地下通道") || value.contains("地下") || value.contains("地道") || value.contains("下穿")) {
            return new NavAction("underpass", "ic_nav_underpass", R.drawable.ic_nav_underpass, "进入地下通道");
        }
        if (value.contains("天桥") || value.contains("过街桥") || value.contains("过街天桥")) {
            return new NavAction("overpass", "ic_nav_overpass", R.drawable.ic_nav_overpass, "上天桥");
        }
        if (value.contains("掉头") || value.contains("调头")) {
            return new NavAction("uturn", "ic_nav_uturn", R.drawable.ic_nav_uturn, "掉头");
        }
        if (value.contains("左前方") || value.contains("靠左") || value.contains("向左前方")) {
            return new NavAction("slight_left", "ic_nav_slight_left", R.drawable.ic_nav_slight_left, "向左前方");
        }
        if (value.contains("右前方") || value.contains("靠右") || value.contains("向右前方")) {
            return new NavAction("slight_right", "ic_nav_slight_right", R.drawable.ic_nav_slight_right, "向右前方");
        }
        if (value.contains("左转") || value.contains("向左")) {
            return new NavAction("turn_left", "ic_nav_turn_left", R.drawable.ic_nav_turn_left, "左转");
        }
        if (value.contains("右转") || value.contains("向右")) {
            return new NavAction("turn_right", "ic_nav_turn_right", R.drawable.ic_nav_turn_right, "右转");
        }
        if (value.contains("直行") || value.contains("继续")) {
            return new NavAction("straight", "ic_nav_straight", R.drawable.ic_nav_straight, "直行");
        }
        return NavAction.unknown();
    }

    private NavAction actionFromIconType(int iconType) {
        switch (iconType) {
            case IconType.LEFT:
            case IconType.LEFT_BACK:
            case IconType.ENTRY_RING_LEFT:
            case IconType.ENTRY_LEFT_RING_LEFT:
            case IconType.MERGE_LEFT:
                return new NavAction("turn_left", "ic_nav_turn_left", R.drawable.ic_nav_turn_left, "左转");
            case IconType.RIGHT:
            case IconType.RIGHT_BACK:
            case IconType.ENTRY_RING_RIGHT:
            case IconType.ENTRY_LEFT_RING_RIGHT:
            case IconType.MERGE_RIGHT:
                return new NavAction("turn_right", "ic_nav_turn_right", R.drawable.ic_nav_turn_right, "右转");
            case IconType.LEFT_FRONT:
            case IconType.ENTRY_LEFT_RING:
                return new NavAction("slight_left", "ic_nav_slight_left", R.drawable.ic_nav_slight_left, "向左前方");
            case IconType.RIGHT_FRONT:
            case IconType.LEAVE_LEFT_RING:
                return new NavAction("slight_right", "ic_nav_slight_right", R.drawable.ic_nav_slight_right, "向右前方");
            case IconType.LEFT_TURN_AROUND:
            case IconType.U_TURN_RIGHT:
            case IconType.ENTRY_RING_UTURN:
            case IconType.ENTRY_LEFTRINGU_TURN:
                return new NavAction("uturn", "ic_nav_uturn", R.drawable.ic_nav_uturn, "掉头");
            case IconType.STRAIGHT:
            case IconType.SPECIAL_CONTINUE:
            case IconType.ENTRY_RING_CONTINUE:
            case IconType.ENTRY_LEFT_RING_CONTINUE:
            case IconType.CROSSWALK:
            case IconType.WALK_ROAD:
                return new NavAction("straight", "ic_nav_straight", R.drawable.ic_nav_straight, "直行");
            case IconType.OVERPASS:
            case IconType.BRIDGE:
            case IconType.SKY_CHANNEL:
                return new NavAction("overpass", "ic_nav_overpass", R.drawable.ic_nav_overpass, "上天桥");
            case IconType.UNDERPASS:
            case IconType.SUBWAY:
            case IconType.CHANNEL:
                return new NavAction("underpass", "ic_nav_underpass", R.drawable.ic_nav_underpass, "进入地下通道");
            case IconType.STAIRCASE:
            case IconType.BY_STAIR:
            case IconType.LADDER:
                return new NavAction("stairs_up", "ic_nav_stairs_up", R.drawable.ic_nav_stairs_up, "楼梯");
            default:
                return NavAction.unknown();
        }
    }

    private String buildActionText(String label, String navigationText, Object distanceValue) {
        if (navigationText != null && !navigationText.trim().isEmpty()) {
            return navigationText;
        }

        int distance = toInt(distanceValue, -1);
        if (distance >= 0 && label != null && !label.isEmpty()) {
            return distance + "米后" + label;
        }
        return label == null || label.isEmpty() ? "等待导航动作" : label;
    }

    private void updateNavActionUi(NavAction navAction) {
        runOnUiThread(() -> {
            if (ivNavAction != null) {
                ivNavAction.setImageResource(navAction.drawableRes);
            }
            if (tvNavAction != null) {
                tvNavAction.setText(navAction.actionText);
            }
        });
    }

    private int toInt(Object value, int defaultValue) {
        if (value instanceof Number) {
            return Math.round(((Number) value).floatValue());
        }
        if (value instanceof String) {
            try {
                return Math.round(Float.parseFloat((String) value));
            } catch (NumberFormatException ignored) {
            }
        }
        return defaultValue;
    }

    private Object readValue(Object target, String... methodNames) {
        if (target == null) {
            return null;
        }

        for (String methodName : methodNames) {
            try {
                Method method = target.getClass().getMethod(methodName);
                return method.invoke(target);
            } catch (Exception ignored) {
            }
        }
        return null;
    }

    private void putIfPresent(JSONObject jsonObject, String key, Object value) throws JSONException {
        if (value == null) {
            return;
        }

        if (value instanceof Number) {
            Number number = (Number) value;
            if ("lat".equals(key) || "lng".equals(key)) {
                double roundedCoordinate = Math.round(number.doubleValue() * 1000000d) / 1000000d;
                jsonObject.put(key, roundedCoordinate);
            } else {
                jsonObject.put(key, Math.round(number.doubleValue()));
            }
        } else if (value instanceof Boolean || value instanceof String) {
            jsonObject.put(key, value);
        } else {
            jsonObject.put(key, String.valueOf(value));
        }
    }

    private static class NavAction {
        final String action;
        final String iconName;
        final int drawableRes;
        final String label;
        final String actionText;

        NavAction(String action, String iconName, int drawableRes, String label) {
            this(action, iconName, drawableRes, label, label);
        }

        NavAction(String action, String iconName, int drawableRes, String label, String actionText) {
            this.action = action;
            this.iconName = iconName;
            this.drawableRes = drawableRes;
            this.label = label;
            this.actionText = actionText;
        }

        NavAction withText(String actionText) {
            return new NavAction(action, iconName, drawableRes, label, actionText);
        }

        static NavAction unknown() {
            return new NavAction("unknown", "ic_nav_unknown", R.drawable.ic_nav_unknown, "未知方向", "等待导航动作");
        }
    }

    // ======================
    // 固定导航回调（无需修改）
    // ======================
    @Override
    public void onInitNaviFailure() {}
    @Override
    public void onInitNaviSuccess() {}
    @Override
    public void onStartNavi(int type) {}
    @Override
    public void onTrafficStatusUpdate() {}
    @Override
    public void onLocationChange(AMapNaviLocation location) {}
    @Override
    public void onGetNavigationText(int type, String text) {
        latestNavigationText = text == null ? "" : text;
    }
    @Override
    public void onGetNavigationText(String text) {
        latestNavigationText = text == null ? "" : text;
    }
    @Override
    public void onEndEmulatorNavi() {}
    @Override
    public void onArriveDestination() {}
    @Override
    public void onCalculateRouteFailure(int errorCode) {}
    @Override
    public void onReCalculateRouteForYaw() {}
    @Override
    public void onReCalculateRouteForTrafficJam() {}
    @Override
    public void onArrivedWayPoint(int wayPointID) {}
    @Override
    public void onGpsOpenStatus(boolean isOpen) {}
    @Override
    public void onNaviInfoUpdate(NaviInfo naviInfo) {
        sendNaviInfoJson(naviInfo);
    }
    @Override
    public void updateCameraInfo(AMapNaviCameraInfo[] cameraInfos) {}
    @Override
    public void updateIntervalCameraInfo(AMapNaviCameraInfo var1, AMapNaviCameraInfo var2, int var3) {}
    @Override
    public void onServiceAreaUpdate(AMapServiceAreaInfo[] serviceAreaInfos) {}
    @Override
    public void showCross(AMapNaviCross cross) {}
    @Override
    public void hideCross() {}
    @Override
    public void showModeCross(AMapModelCross modelCross) {}
    @Override
    public void hideModeCross() {}
    @Override
    public void showLaneInfo(AMapLaneInfo[] laneInfos, byte[] laneBackgroundInfo, byte[] laneRecommendedInfo) {}
    @Override
    public void showLaneInfo(AMapLaneInfo laneInfo) {}
    @Override
    public void hideLaneInfo() {}
    @Override
    public void onCalculateRouteSuccess(int[] routeIds) {}
    @Override
    public void notifyParallelRoad(int type) {}
    @Override
    public void OnUpdateTrafficFacility(AMapNaviTrafficFacilityInfo[] trafficFacilityInfos) {}
    @Override
    public void OnUpdateTrafficFacility(AMapNaviTrafficFacilityInfo trafficFacilityInfo) {}
    @Override
    public void updateAimlessModeStatistics(AimLessModeStat stat) {}
    @Override
    public void updateAimlessModeCongestionInfo(AimLessModeCongestionInfo congestionInfo) {}
    @Override
    public void onPlayRing(int type) {}
    @Override
    public void onNaviRouteNotify(AMapNaviRouteNotifyData notifyData) {}
    @Override
    public void onGpsSignalWeak(boolean isWeak) {}

    @Override
    public void onNaviSetting() {}
    @Override
    public void onNaviCancel() { finish(); }
    @Override
    public boolean onNaviBackClick() { return false; }
    @Override
    public void onNaviMapMode(int mode) {}
    @Override
    public void onNaviTurnClick() {}
    @Override
    public void onNextRoadClick() {}
    @Override
    public void onScanViewButtonClick() {}
    @Override
    public void onLockMap(boolean isLock) {}
    @Override
    public void onNaviViewLoaded() {}
    @Override
    public void onMapTypeChanged(int type) {}
    @Override
    public void onNaviViewShowMode(int mode) {}
    @Override
    public void onStopSpeaking() {}
    @Override
    public void onViewTypeChanged(AmapPageType type) {}
    @Override
    public void onAMapNaviViewExit() {}
    @Override
    public void onStrategyChanged(int strategy) {}
    @Override
    public void onBroadcastModeChanged(int mode) {}
    @Override
    public void onDayAndNightModeChanged(int mode) {}
    @Override
    public void onScaleAutoChanged(boolean enable) {}
    @Override
    public void onListenToVoiceDuringCallChanged(boolean enable) {}
    @Override
    public void onControlMusicVolumeModeChanged(int mode) {}
    @Override
    public void onEagleChanged(boolean enable) {}
    @Override
    public void onNaviRouteHighlightChange(long var1, int var3) {}

    // ======================
    // 导航视图生命周期
    // ======================
    @Override
    protected void onResume() {
        super.onResume();
        mAMapNaviView.onResume();
    }

    @Override
    protected void onPause() {
        super.onPause();
        mAMapNaviView.onPause();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        mAMapNaviView.onSaveInstanceState(outState);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        mAMapNaviView.onDestroy();
        if (bluetoothJsonSender != null) {
            bluetoothJsonSender.close();
        }
        if (macBleJsonSender != null) {
            macBleJsonSender.close();
        }
        if (mAMapNavi != null) {
            mAMapNavi.removeAMapNaviListener(this);
            mAMapNavi.destroy();
        }
    }

    // ======================
    // 权限申请
    // ======================
    private void checkAndRequestNavigationPermissions() {
        List<String> permissions = new ArrayList<>();
        addRequiredPermission(permissions, Manifest.permission.ACCESS_FINE_LOCATION);
        addRequiredPermission(permissions, Manifest.permission.ACCESS_COARSE_LOCATION);

        if (!permissions.isEmpty()) {
            ActivityCompat.requestPermissions(
                    this,
                    permissions.toArray(new String[0]),
                    PERMISSION_REQUEST_CODE
            );
        }
    }

    private void checkAndRequestBluetoothPermissions() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) {
            return;
        }

        List<String> permissions = new ArrayList<>();
        addRequiredPermission(permissions, Manifest.permission.BLUETOOTH_CONNECT);
        addRequiredPermission(permissions, Manifest.permission.BLUETOOTH_SCAN);

        if (!permissions.isEmpty()) {
            ActivityCompat.requestPermissions(
                    this,
                    permissions.toArray(new String[0]),
                    BLUETOOTH_PERMISSION_REQUEST_CODE
            );
        }
    }

    private void addRequiredPermission(List<String> permissions, String permission) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
            permissions.add(permission);
        }
    }
}