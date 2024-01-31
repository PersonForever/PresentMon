// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
import { Module, VuexModule, Mutation, Action, getModule  } from 'vuex-module-decorators'
import store from './index'
import { Metric } from '@/core/metric'
import { Stat } from '@/core/stat'
import { Unit } from '@/core/unit'
import { Api } from '@/core/api'

@Module({name: 'introspection', dynamic: true, store, namespaced: true})
export class IntrospectionModule extends VuexModule {
  metrics: Metric[] = [];
  stats: Stat[] = [];
  units: Unit[] = [];

  @Mutation
  replaceAllMetrics(metrics: Metric[]) {
    this.metrics = metrics;
  }

  @Mutation
  replaceAllStats(stats: Stat[]) {
    this.stats = stats;
  }

  @Mutation
  replaceAllUnits(units: Unit[]) {
    this.units = units;
  }

  @Action({rawError: true})
  async load() {
    if (this.metrics.length === 0) {
      const intro = await Api.introspect();
      this.context.commit('replaceAllMetrics', intro.metrics);
      this.context.commit('replaceAllStats', intro.stats);
      this.context.commit('replaceAllUnits', intro.units);
    }
  }
}

export const Introspection = getModule(IntrospectionModule);