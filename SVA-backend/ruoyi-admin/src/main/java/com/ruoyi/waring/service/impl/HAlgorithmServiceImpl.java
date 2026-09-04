package com.ruoyi.waring.service.impl;

import com.ruoyi.waring.domain.HAlgorithm;
import com.ruoyi.waring.mapper.HAlgorithmMapper;
import com.ruoyi.waring.service.HAlgorithmService;
import com.ruoyi.common.utils.StringUtils;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.stream.Collectors;

@Service
public class HAlgorithmServiceImpl implements HAlgorithmService {
    private static final String SLEEP_POSE_ALGORITHM_CODE = "on_yolo11n_pose";
    private static final String SLEEP_POSE_ALGORITHM_NAME = "YOLO11n-Pose 睡岗检测";

    @Autowired
    HAlgorithmMapper hAlgorithmMapper;

    @Override
    public List<HAlgorithm> selectAlgorithmList() {
        List<HAlgorithm> algorithms = hAlgorithmMapper.selectWaringList();
        List<HAlgorithm> result = algorithms == null
                ? new ArrayList<>()
                : new ArrayList<>(algorithms);
        boolean poseAlgorithmExists = result.stream().anyMatch(this::isSleepPoseAlgorithm);
        if (!poseAlgorithmExists) {
            HAlgorithm poseAlgorithm = new HAlgorithm();
            poseAlgorithm.setCode(SLEEP_POSE_ALGORITHM_CODE);
            poseAlgorithm.setName(SLEEP_POSE_ALGORITHM_NAME);
            poseAlgorithm.setAlgorithmCode(SLEEP_POSE_ALGORITHM_CODE);
            poseAlgorithm.setAlgorithmName(SLEEP_POSE_ALGORITHM_NAME);
            poseAlgorithm.setAlgorithm_code(SLEEP_POSE_ALGORITHM_CODE);
            poseAlgorithm.setAlgorithm_name(SLEEP_POSE_ALGORITHM_NAME);
            poseAlgorithm.setVersion("v0.1");
            poseAlgorithm.setStatus("0");
            result.add(poseAlgorithm);
        }
        return result;
    }

    @Override
    public List<String> selectTargetsByCode(String code) {
        if (StringUtils.isBlank(code)) {
            return Collections.emptyList();
        }

        String objectStr = getObjectStrByCode(code);
        if (objectStr == null || objectStr.trim().isEmpty()) {
            return Collections.emptyList();
        }

        LinkedHashSet<String> orderedUniqueTargets = new LinkedHashSet<>();
        for (String item : objectStr.split(",")) {
            if (item == null) {
                continue;
            }
            String target = item.trim();
            if (!target.isEmpty()) {
                orderedUniqueTargets.add(target);
            }
        }

        return orderedUniqueTargets.stream().collect(Collectors.toList());
    }

    @Override
    public String getObjectStrByCode(String code) {
        String objectStr = hAlgorithmMapper.selectObjectStrByCode(code);
        if (StringUtils.isBlank(objectStr) && SLEEP_POSE_ALGORITHM_CODE.equals(code)) {
            return "person";
        }
        return objectStr;
    }

    @Override
    public String getApiUrlByCode(String code) {
        return hAlgorithmMapper.selectApiUrlByCode(code);
    }

    @Override
    public String getNameByCode(String code) {
        String name = hAlgorithmMapper.selectNameByCode(code);
        if (StringUtils.isBlank(name) && SLEEP_POSE_ALGORITHM_CODE.equals(code)) {
            return SLEEP_POSE_ALGORITHM_NAME;
        }
        return name;
    }

    private boolean isSleepPoseAlgorithm(HAlgorithm algorithm) {
        if (algorithm == null) {
            return false;
        }
        return SLEEP_POSE_ALGORITHM_CODE.equals(algorithm.getCode())
                || SLEEP_POSE_ALGORITHM_CODE.equals(algorithm.getAlgorithmCode())
                || SLEEP_POSE_ALGORITHM_CODE.equals(algorithm.getAlgorithm_code());
    }
}
