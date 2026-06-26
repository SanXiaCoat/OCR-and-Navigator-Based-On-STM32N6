package com.sanxia.stm32ARmap;

import android.os.Bundle;
import androidx.appcompat.app.AppCompatActivity;
import com.amap.api.navi.AMapNavi;
import com.amap.api.navi.AMapNaviListener;
import com.amap.api.navi.enums.NaviType;
import com.amap.api.navi.model.*;
import java.util.ArrayList;
import java.util.List;

public class NaviActivity extends AppCompatActivity implements AMapNaviListener {
    private AMapNavi aMapNavi;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            // 1. 初始化导航实例
            aMapNavi = AMapNavi.getInstance(getApplicationContext());
            aMapNavi.addAMapNaviListener(this);

            // 2. 开启内置语音播报（无需集成第三方TTS）
            aMapNavi.setUseInnerVoice(true);

            // 3. 设置起点和终点（示例：北京西站 -> 天安门）
            NaviLatLng startPoint = new NaviLatLng(39.92504, 116.43799);
            NaviLatLng endPoint = new NaviLatLng(39.90346, 116.39151);

            List<NaviLatLng> startList = new ArrayList<>();
            List<NaviLatLng> endList = new ArrayList<>();
            startList.add(startPoint);
            endList.add(endPoint);

            // 4. 计算驾车路线（策略：10=躲避拥堵）
            aMapNavi.calculateDriveRoute(startList, endList, null, 10);

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // ==========================================
    // 核心回调（你主要需要关注这两个）
    // ==========================================

    /**
     * 路线计算成功回调 - 这里启动导航
     */
    @Override
    public void onCalculateRouteSuccess(AMapCalcRouteResult result) {
        // 启动实时导航
        if (aMapNavi != null) {
            aMapNavi.startNavi(NaviType.GPS);
        }
    }

    /**
     * 旧版路线计算成功回调（保留空实现即可）
     */
    @Override
    public void onCalculateRouteSuccess(int[] routeIds) {
    }

    /**
     * 路线计算失败回调
     */
    @Override
    public void onCalculateRouteFailure(AMapCalcRouteResult result) {
        // 可以在这里提示用户：路线规划失败，请检查网络
    }

    /**
     * 旧版路线计算失败回调（保留空实现即可）
     */
    @Override
    public void onCalculateRouteFailure(int errorCode) {
    }

    // ==========================================
    // 初始化相关回调
    // ==========================================

    @Override
    public void onInitNaviFailure() {
        // 导航初始化失败
    }

    @Override
    public void onInitNaviSuccess() {
        // 导航初始化成功
    }

    // ==========================================
    // 导航状态相关回调（按需要实现逻辑）
    // ==========================================

    @Override
    public void onStartNavi(int type) {
        // 开始导航
    }

    @Override
    public void onEndEmulatorNavi() {
        // 模拟导航结束
    }

    @Override
    public void onArriveDestination() {
        // 到达目的地
    }

    @Override
    public void onArrivedWayPoint(int wayPointID) {
        // 到达途经点
    }

    // ==========================================
    // 位置与交通信息回调
    // ==========================================

    @Override
    public void onLocationChange(AMapNaviLocation location) {
        // 位置变化回调
    }

    @Override
    public void onTrafficStatusUpdate() {
        // 路况更新
    }

    @Override
    public void onGpsOpenStatus(boolean isOpen) {
        // GPS开关状态变化
    }

    @Override
    public void onGpsSignalWeak(boolean isWeak) {
        // GPS信号弱回调
    }

    // ==========================================
    // 导航播报与提示回调
    // ==========================================

    @Override
    public void onGetNavigationText(int type, String text) {
        // 获取导航文字（带类型）
    }

    @Override
    public void onGetNavigationText(String text) {
        // 获取导航文字
    }

    @Override
    public void onPlayRing(int type) {
        // 播放提示音
    }

    // ==========================================
    // 路口、车道、电子眼等导航视图回调
    // ==========================================

    @Override
    public void onNaviInfoUpdate(NaviInfo naviInfo) {
        // 导航信息更新（如剩余距离、时间等）
    }

    @Override
    public void showCross(AMapNaviCross cross) {
        // 显示普通路口放大图
    }

    @Override
    public void hideCross() {
        // 隐藏普通路口放大图
    }

    @Override
    public void showModeCross(AMapModelCross modelCross) {
        // 显示模型路口放大图
    }

    @Override
    public void hideModeCross() {
        // 隐藏模型路口放大图
    }

    @Override
    public void showLaneInfo(AMapLaneInfo[] laneInfos, byte[] laneBackgroundInfo, byte[] laneRecommendedInfo) {
        // 显示车道信息（完整版）
    }

    @Override
    public void showLaneInfo(AMapLaneInfo laneInfo) {
        // 显示车道信息
    }

    @Override
    public void hideLaneInfo() {
        // 隐藏车道信息
    }

    @Override
    public void updateCameraInfo(AMapNaviCameraInfo[] cameraInfos) {
        // 更新电子眼信息
    }

    @Override
    public void updateIntervalCameraInfo(AMapNaviCameraInfo var1, AMapNaviCameraInfo var2, int var3) {
        // 更新区间测速信息
    }

    @Override
    public void OnUpdateTrafficFacility(AMapNaviTrafficFacilityInfo[] trafficFacilityInfos) {
        // 更新交通设施信息（数组版）
    }

    @Override
    public void OnUpdateTrafficFacility(AMapNaviTrafficFacilityInfo trafficFacilityInfo) {
        // 更新交通设施信息（单个版）
    }

    @Override
    public void onServiceAreaUpdate(AMapServiceAreaInfo[] serviceAreaInfos) {
        // 服务区信息更新
    }

    // ==========================================
    // 其他回调
    // ==========================================

    @Override
    public void onReCalculateRouteForYaw() {
        // 偏航重算
    }

    @Override
    public void onReCalculateRouteForTrafficJam() {
        // 拥堵重算
    }

    @Override
    public void notifyParallelRoad(int type) {
        // 主辅路切换通知
    }

    @Override
    public void updateAimlessModeStatistics(AimLessModeStat stat) {
        // 巡航模式统计信息更新
    }

    @Override
    public void updateAimlessModeCongestionInfo(AimLessModeCongestionInfo congestionInfo) {
        // 巡航模式拥堵信息更新
    }

    @Override
    public void onNaviRouteNotify(AMapNaviRouteNotifyData notifyData) {
        // 导航路线通知
    }

    // ==========================================
    // 生命周期与资源释放
    // ==========================================

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // 重要：退出时释放导航资源，防止内存泄漏
        if (aMapNavi != null) {
            aMapNavi.removeAMapNaviListener(this);
            aMapNavi.destroy();
            aMapNavi = null;
        }
    }
}