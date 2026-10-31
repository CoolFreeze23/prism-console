<script setup lang="ts">
import { t, type MessageKey } from './i18n'
import { computed, ref } from 'vue'
import { invoke } from './bridge'
const props = defineProps<{ values: Record<string, string | number | boolean>; instance?: boolean }>()
const section = ref('Memory'), error = ref(''), detecting = ref(false)
const java = ref<{ path: string; version: string; architecture: string }[]>([])
const groups: Record<string, string[]> = {
  Memory: ['OverrideMemory', 'MinMemAlloc', 'MaxMemAlloc'],
  Java: ['OverrideJavaLocation', 'AutomaticJavaSwitch', 'AutomaticJavaDownload', 'JavaPath', 'OverrideJavaArgs', 'JvmArgs'],
  Window: ['OverrideWindow', 'LaunchMaximized', 'MinecraftWinWidth', 'MinecraftWinHeight'],
  Performance: ['OverridePerformance', 'EnableFeralGamemode', 'EnableMangoHud', 'UseDiscreteGpu', 'OverrideGameTime', 'RecordGameTime', 'NumberOfConcurrentDownloads'],
  Commands: ['OverrideCommands', 'PreLoadCommand', 'PreLaunchCommand', 'WrapperCommand', 'PostExitCommand'],
}
const keys = computed(() => groups[section.value].filter(key => key in props.values))
function inherited(key: string) {
  if (!props.instance || key.startsWith('Override')) return false
  const gate = key === 'JavaPath' ? 'OverrideJavaLocation' : key === 'JvmArgs' ? 'OverrideJavaArgs' : key === 'RecordGameTime' ? 'OverrideGameTime' : ({ Memory: 'OverrideMemory', Window: 'OverrideWindow', Performance: 'OverridePerformance', Commands: 'OverrideCommands' } as Record<string, string>)[section.value]
  return !!gate && !props.values[gate]
}
async function detect() {
  detecting.value = true; error.value = ''
  try { await invoke('detectJava') } catch (e) { error.value = String(e) } finally { detecting.value = false }
}
async function refreshJava() {
  try { java.value = await invoke('javaInstallations') } catch (e) { error.value = String(e) }
}
</script>
<template>
  <nav class="actions" :aria-label="t('settings.settings_sections')"><button v-for="(_, title) in groups" :key="title" :aria-pressed="section === title" @click="section = title">{{ t(`settings.group.${title}` as MessageKey) }}</button></nav>
  <p v-if="instance">{{ t('settings.overrides_apply_only_to_this') }}</p>
  <p v-if="error" role="alert">{{ error }}</p>
  <p v-if="section === 'Commands'">{{ t('settings.hooks_execute_local_commands_when') }}</p>
  <div class="preferences-list">
    <template v-for="key in keys" :key="key">
      <button v-if="typeof values[key] === 'boolean'" class="row setting-toggle" role="switch" :disabled="inherited(key)" :aria-checked="!!values[key]" @click="values[key] = !values[key]"><span class="setting-text"><span>{{ t(`settings.label.${key}` as MessageKey) }}</span></span><span class="switch" aria-hidden="true"><span /></span></button>
      <label v-else-if="typeof values[key] === 'number'">{{ t(`settings.label.${key}` as MessageKey) }}<input type="number" v-model.number="values[key]" :disabled="inherited(key)"></label>
      <label v-else>{{ t(`settings.label.${key}` as MessageKey) }}<textarea rows="2" v-model="values[key]" :disabled="inherited(key)" maxlength="16384" /></label>
    </template>
  </div>
  <template v-if="section === 'Java'">
    <div class="actions"><button :disabled="detecting" @click="detect">{{ t('settings.detect_java') }}</button><button @click="refreshJava">{{ t('settings.refresh_detected_installations') }}</button></div>
    <p>{{ t('settings.detection_runs_in_activity_refresh') }}</p>
    <button v-for="j in java" :key="j.path" class="row" @click="values.JavaPath = j.path; instance && (values.OverrideJavaLocation = true)"><span>Java {{ j.version }} · {{ j.architecture }}<small>{{ j.path }}</small></span><span>{{ t('settings.use') }}</span></button>
  </template>
</template>
