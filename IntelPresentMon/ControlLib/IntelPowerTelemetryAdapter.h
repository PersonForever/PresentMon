// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#define NOMINMAX
#include <Windows.h>
#include "igcl_api.h"
#include "PowerTelemetryAdapter.h"
#include "TelemetryHistory.h"
#include "ctlpvttemp_api.h"
#include <mutex>
#include <optional>
#include <variant>

namespace pwr::intel
{
	class IntelPowerTelemetryAdapter : public PowerTelemetryAdapter
	{
	public:
		IntelPowerTelemetryAdapter(ctl_device_adapter_handle_t handle);
		bool Sample() noexcept override;
		std::optional<PresentMonPowerTelemetryInfo> GetClosest(uint64_t qpc) const noexcept override;
		PM_DEVICE_VENDOR GetVendor() const noexcept override;
		std::string GetName() const noexcept override;
        uint64_t GetDedicatedVideoMemory() const noexcept override;
		uint64_t GetVideoMemoryMaxBandwidth() const noexcept override;
		double GetSustainedPowerLimit() const noexcept override;

		// types
		class NonGraphicsDeviceException : public std::exception {};

	private:
		// functions


		ctl_result_t EnumerateMemoryModules();

		template<class T>
		ctl_result_t GetTimeDelta(const T& power_telemetry);

		// TODO: meld these into the sample function
		template<class T>
		ctl_result_t GetGPUPowerTelemetryData(
			const T& power_telemetry,
			PresentMonPowerTelemetryInfo& pm_gpu_power_telemetry_info);
		template<class T>
		ctl_result_t GetVramPowerTelemetryData(
			const T& power_telemetry,
			PresentMonPowerTelemetryInfo& pm_gpu_power_telemetry_info);
		template<class T>
		ctl_result_t GetFanPowerTelemetryData(
			const T& power_telemetry,
			PresentMonPowerTelemetryInfo& pm_gpu_power_telemetry_info);
		template<class T>
		ctl_result_t GetPsuPowerTelemetryData(
			const T& power_telemetry,
			PresentMonPowerTelemetryInfo& pm_gpu_power_telemetry_info);

		void GetMemStateTelemetryData(
			const ctl_mem_state_t& mem_state,
			PresentMonPowerTelemetryInfo& pm_gpu_power_telemetry_info);
		void GetMemBandwidthData(
			const ctl_mem_bandwidth_t& mem_bandwidth,
			PresentMonPowerTelemetryInfo& pm_gpu_power_telemetry_info);

		void SavePmPowerTelemetryData(PresentMonPowerTelemetryInfo& pm_gpu_power_telemetry_info);
		template<class T>
        ctl_result_t SaveTelemetry(
            const T& power_telemetry,
            const ctl_mem_bandwidth_t& mem_bandwidth);

		// TODO: put these as part of the telemetry data object
		ctl_result_t GetInstantaneousPowerTelemetryItem(
			const ctl_oc_telemetry_item_t& telemetry_item,
			double& pm_telemetry_value,
			GpuTelemetryCapBits telemetry_cap_bit);
		ctl_result_t GetPowerTelemetryItemUsagePercent(
			const ctl_oc_telemetry_item_t& current_telemetry_item,
			const ctl_oc_telemetry_item_t& previous_telemetry_item,
			double& pm_telemetry_value,
			GpuTelemetryCapBits telemetry_cap_bit);
		ctl_result_t GetPowerTelemetryItemUsage(
			const ctl_oc_telemetry_item_t& current_telemetry_item,
			const ctl_oc_telemetry_item_t& previous_telemetry_item,
			double& pm_telemetry_value,
			GpuTelemetryCapBits telemetry_cap_bit);
        GpuTelemetryCapBits GetFlagTelemetryCapBit(uint32_t fan_index);
		GpuTelemetryCapBits GetPsuTelemetryCapBit(uint32_t psu_index);
		// data
		ctl_device_adapter_handle_t deviceHandle = nullptr;
		LUID deviceId; // pointed to by a device_adapter_properties member, written to by igcl api
		ctl_device_adapter_properties_t properties{};
		std::vector<ctl_mem_handle_t> memoryModules;
		mutable std::mutex historyMutex;
		TelemetryHistory<PresentMonPowerTelemetryInfo> history{ PowerTelemetryAdapter::defaultHistorySize };
		std::variant<std::monostate, ctl_power_telemetry_t, ctl_power_telemetry2_t> previousSampleVariant;
		double time_delta_ = 0.f;
		// this cache is only used with old V0 api bandwidth counters
		uint64_t gpu_mem_max_bw_cache_value_bps_ = 0;
	};
}