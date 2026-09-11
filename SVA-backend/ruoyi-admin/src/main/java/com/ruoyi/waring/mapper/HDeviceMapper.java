package com.ruoyi.waring.mapper;

import com.ruoyi.waring.domain.HDevice;
import org.apache.ibatis.annotations.Mapper;
import org.apache.ibatis.annotations.Param;
import org.springframework.stereotype.Repository;

import java.util.List;


@Mapper
@Repository
public interface HDeviceMapper {

    void insertDevice(HDevice device);

    void deleteDevice();

    HDevice selectDeviceByApeId(String apeId);

    HDevice selectByGbDeviceId(@Param("gbDeviceId") String gbDeviceId);

    int insertDeviceCrud(HDevice device);

    int updateDevice(HDevice device);

    int deleteDeviceByApeIds(@Param("apeIds") String[] apeIds);

    int updateMonitorStateByApeId(@Param("apeId") String apeId,
                                  @Param("monitorStatus") String monitorStatus);

    int updatePlayUrlByApeId(@Param("apeId") String apeId,
                             @Param("playUrl") String playUrl);

    int updateZlmProxyKeyByApeId(@Param("apeId") String apeId,
                                 @Param("zlmProxyKey") String zlmProxyKey);

    /**
     * 将本轮国标同步中未出现的本地 GB28181 设备置为离线。
     * activeIds 为空时，表示 ZLM 当前无任何国标设备，全部置为离线。
     */
    int updateGbDeviceOffline(@Param("activeIds") List<String> activeIds);

    List<HDevice> selectDeviceList(HDevice device);

    List<HDevice> selectLDeviceList(HDevice device);

    int getDeviceNum();

    int getDeviceNumByOrg(HDevice device);

    int getDeviceEnableNum();

    int getDeviceEnableNumByOrg(HDevice device);


}
