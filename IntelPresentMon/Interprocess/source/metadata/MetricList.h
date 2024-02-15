#pragma once
#include "../../../PresentMonAPI2/source/PresentMonAPI.h"
#include "StatsShortcuts.h"

#define METRIC_LIST(X_) \
		X_(PM_METRIC_SWAP_CHAIN_ADDRESS, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_UINT64, PM_DATA_TYPE_UINT64, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_DISPLAYED_FPS, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_FPS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_VOID, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_PRESENTED_FPS, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_FPS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_VOID, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_FRAME_DURATION, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_VOID, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_GPU_BUSY_TIME, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_GPU_WAIT_TIME, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_DISPLAY_DURATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_DROPPED_FRAMES, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_NUM_FRAMES, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_INT32, PM_DATA_TYPE_VOID, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_SYNC_INTERVAL, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_VERTICAL_BLANKS, PM_DATA_TYPE_UINT32, PM_DATA_TYPE_UINT32, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_PRESENT_MODE, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_ENUM, PM_DATA_TYPE_ENUM, PM_ENUM_PRESENT_MODE, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_PRESENT_RUNTIME, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_ENUM, PM_DATA_TYPE_ENUM, PM_ENUM_GRAPHICS_RUNTIME, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_CPU_FRAME_QPC, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_QPC, PM_DATA_TYPE_UINT64, PM_DATA_TYPE_UINT64, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_ALLOWS_TEARING, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_GPU_LATENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_DISPLAY_LATENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_INPUT_LATENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		\
		X_(PM_METRIC_GPU_POWER, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_WATTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_VOLTAGE, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_VOLTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MEGAHERTZ, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_TEMPERATURE, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_CELSIUS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_FAN_SPEED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_RPM, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_MEDIA_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_POWER, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_WATTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_VOLTAGE, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_VOLTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_FREQUENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MEGAHERTZ, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_EFFECTIVE_FREQUENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MEGAHERTZ, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_TEMPERATURE, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_CELSIUS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_MEM_USED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BYTES, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_UINT64, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_MEM_UTILIZATION, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_VOID, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_MEM_WRITE_BANDWIDTH, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BITS_PER_SECOND, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_MEM_READ_BANDWIDTH, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BITS_PER_SECOND, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_POWER_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_TEMPERATURE_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_CURRENT_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_VOLTAGE_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_GPU_UTILIZATION_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_POWER_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_TEMPERATURE_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_CURRENT_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_VOLTAGE_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_VRAM_UTILIZATION_LIMITED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BOOLEAN, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_BOOL, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		\
		X_(PM_METRIC_CPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_CPU_POWER, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_WATTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_CPU_TEMPERATURE, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_CELSIUS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_CPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MEGAHERTZ, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, FULL_STATS) \
		X_(PM_METRIC_CPU_CORE_UTILITY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_VOID, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		\
		X_(PM_METRIC_APPLICATION, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_STRING, PM_DATA_TYPE_STRING, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_GPU_SUSTAINED_POWER_LIMIT, PM_METRIC_TYPE_STATIC, PM_UNIT_WATTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_STAT_MID_POINT) \
		X_(PM_METRIC_GPU_MEM_SIZE, PM_METRIC_TYPE_STATIC, PM_UNIT_BYTES, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_UINT64, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_STAT_MID_POINT) \
		X_(PM_METRIC_GPU_MEM_MAX_BANDWIDTH, PM_METRIC_TYPE_STATIC, PM_UNIT_BITS_PER_SECOND, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_UINT64, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_STAT_MID_POINT) \
		X_(PM_METRIC_GPU_VENDOR, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_ENUM, PM_DATA_TYPE_ENUM, PM_ENUM_DEVICE_VENDOR, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_STAT_MID_POINT) \
		X_(PM_METRIC_GPU_NAME, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_STRING, PM_DATA_TYPE_STRING, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_STAT_MID_POINT) \
		X_(PM_METRIC_CPU_POWER_LIMIT, PM_METRIC_TYPE_STATIC, PM_UNIT_WATTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_STAT_MID_POINT) \
		X_(PM_METRIC_CPU_VENDOR, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_ENUM, PM_DATA_TYPE_ENUM, PM_ENUM_DEVICE_VENDOR, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		X_(PM_METRIC_CPU_NAME, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_STRING, PM_DATA_TYPE_STRING, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_MID_POINT) \
		\
		X_(PM_METRIC_PRESENT_FLAGS, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_UINT32, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_SECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME_IN_PRESENT_API, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME_BETWEEN_PRESENTS, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME_UNTIL_RENDER_COMPLETE, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME_UNTIL_DISPLAYED, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME_BETWEEN_DISPLAY_CHANGE, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME_UNTIL_RENDER_START, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_TIME_SINCE_INPUT, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_GPU_VIDEO_BUSY_TIME, PM_METRIC_TYPE_FRAME_EVENT, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_VOID, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, PM_STAT_NONE) \
		X_(PM_METRIC_CPU_DURATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_CPU_FRAME_PACING_STALL, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS) \
		X_(PM_METRIC_GPU_DURATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE, 0, PM_DEVICE_TYPE_INDEPENDENT, FULL_STATS)
