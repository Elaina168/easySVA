package com.ruoyi.waring.domain;

import java.io.Serializable;

/**
 * 云台控制 (PTZ) 数据传输对象
 */
public class PtzCommandDTO implements Serializable {
    private static final long serialVersionUID = 1L;

    /** 控制指令: up, down, left, right, upleft, upright, downleft, downright, zoomin, zoomout, stop, reset */
    private String command;

    /** 移动速度 (1-255，默认 32) */
    private Integer speed = 32;

    public String getCommand() {
        return command;
    }

    public void setCommand(String command) {
        this.command = command;
    }

    public Integer getSpeed() {
        return speed != null && speed > 0 ? speed : 32;
    }

    public void setSpeed(Integer speed) {
        this.speed = speed;
    }

    @Override
    public String toString() {
        return "PtzCommandDTO{" +
                "command='" + command + '\'' +
                ", speed=" + speed +
                '}';
    }
}
