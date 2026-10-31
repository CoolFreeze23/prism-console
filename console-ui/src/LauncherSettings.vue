<script setup lang="ts">
// Launcher-wide options that matter on a handheld. Every change applies at once, as in Steam's own settings.
import { onMounted, ref } from 'vue'
import { invoke } from './bridge'
import { t, type MessageKey } from './i18n'
import { play, soundSettings } from './sound'
type Options = Record<string, boolean | number> & { about?: { name: string; version: string; commit: string; qt: string } }
const emit = defineEmits<{ error: [message: string] }>()
const values = ref<Options>({})
const sections: { title: MessageKey; rows: { key: string; min?: number; max?: number; step?: number; unit?: string }[] }[] = [
  { title: 'launcher.section.sound', rows: [{ key: 'soundEnabled' }, { key: 'soundVolume', min: 0, max: 100, step: 10, unit: '%' }] },
  { title: 'launcher.section.mods', rows: [{ key: 'installDependencies' }, { key: 'trackMetadata' }, { key: 'askModpackUpdate' }, { key: 'downloadGameFiles' }] },
  { title: 'launcher.section.downloads', rows: [{ key: 'parallelDownloads', min: 1, max: 32, step: 1 }, { key: 'retryLimit', min: 0, max: 10, step: 1 }, { key: 'timeoutSeconds', min: 5, max: 120, step: 5, unit: ' s' }] },
  { title: 'launcher.section.playTime', rows: [{ key: 'recordPlayTime' }, { key: 'showPlayTime' }, { key: 'lowMemoryWarning' }] },
]
async function set(key: string, value: boolean | number) {
  try {
    values.value = { ...values.value, [key]: await invoke<boolean | number>('setLauncherOption', { key, value }) }
    // Sound changes are heard at once, at the new level (their buttons skip the usual press sound).
    if (key === 'soundEnabled') { soundSettings.enabled = !!values.value[key]; play('confirm') }
    if (key === 'soundVolume') { soundSettings.volume = Number(values.value[key]); play('confirm') }
  } catch (e) { emit('error', String(e)) }
}
function step(key: string, direction: number, row: { min?: number; max?: number; step?: number }) {
  const next = Math.min(row.max ?? 100, Math.max(row.min ?? 0, Number(values.value[key] ?? 0) + direction * (row.step ?? 1)))
  if (next !== values.value[key]) set(key, next)
}
onMounted(async () => { try { values.value = await invoke<Options>('launcherOptions') } catch (e) { emit('error', String(e)) } })
</script>

<template>
  <div class="launcher-settings">
    <section v-for="section in sections" :key="section.title">
      <h3 class="section-title">{{ t(section.title) }}</h3>
      <template v-for="row in section.rows" :key="row.key">
        <button v-if="row.max === undefined" class="setting-toggle" role="switch" :data-option="row.key" :data-sound="row.key.startsWith('sound') ? 'none' : undefined" :aria-checked="!!values[row.key]" @click="set(row.key, !values[row.key])">
          <span class="setting-text"><span>{{ t(`launcher.${row.key}` as MessageKey) }}</span><small>{{ t(`launcher.${row.key}.hint` as MessageKey) }}</small></span>
          <span class="switch" aria-hidden="true"><span /></span>
        </button>
        <div v-else class="setting-stepper" :data-option="row.key" :data-sound="row.key.startsWith('sound') ? 'none' : undefined">
          <span class="setting-text"><span>{{ t(`launcher.${row.key}` as MessageKey) }}</span><small>{{ t(`launcher.${row.key}.hint` as MessageKey) }}</small></span>
          <button :aria-label="t('launcher.less')" :disabled="Number(values[row.key]) <= (row.min ?? 0)" @click="step(row.key, -1, row)">−</button>
          <output>{{ values[row.key] }}{{ row.unit || '' }}</output>
          <button :aria-label="t('launcher.more')" :disabled="Number(values[row.key]) >= (row.max ?? 100)" @click="step(row.key, 1, row)">+</button>
        </div>
      </template>
    </section>
    <section v-if="values.about" class="about">
      <h3 class="section-title">{{ t('launcher.section.about') }}</h3>
      <p><strong>Prism Console</strong> · {{ t('launcher.aboutBuiltOn', { name: values.about.name, version: values.about.version }) }}</p>
      <p class="muted">{{ t('launcher.aboutDetails', { commit: values.about.commit || '—', qt: values.about.qt }) }}</p>
      <p class="muted">{{ t('launcher.aboutLicenses') }}</p>
    </section>
  </div>
</template>

<style>
.launcher-settings { max-width: 900px; }
.setting-stepper { display: grid; grid-template-columns: minmax(0, 1fr) auto 76px auto; align-items: center; gap: 12px; padding: 10px 14px; min-height: 64px; border-bottom: 1px solid var(--pc-raised); }
.setting-stepper button { min-width: 52px; font-size: 20px; padding: 6px; }
.setting-stepper output { text-align: center; font-variant-numeric: tabular-nums; font-weight: 600; }
.about p { margin: 6px 0; }
</style>
