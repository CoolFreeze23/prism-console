<script setup lang="ts">
import { t } from './i18n'
import { onMounted, ref, watch } from 'vue'
import { invoke } from './bridge'
import SettingsPane from './SettingsPane.vue'
import FilePicker from './FilePicker.vue'
const props = defineProps<{ mode: string; instanceId?: string }>()
const emit = defineEmits<{ changed: [] }>()
const busy = ref(false), error = ref(''), message = ref('')
const name = ref(''), source = ref('')
const browsing = ref(false)
const notes = ref(''), includeWorlds = ref(false), worlds = ref<{ id: string; name: string; mode: string; lastPlayed: string }[]>([]), world = ref('')
const values = ref<Record<string, string | number | boolean>>({})
const components = ref<{ id: string; name: string; version: string }[]>([])
const loader = ref('net.neoforged'), versions = ref<string[]>([]), version = ref(''), search = ref('')
async function run(job: () => Promise<void>) {
  if (busy.value) return
  busy.value = true; error.value = ''; message.value = ''
  try { await job(); emit('changed') } catch (e) { error.value = String(e) } finally { busy.value = false }
}
async function refresh() {
  if (props.mode === 'preferences') values.value = await invoke('globalSettings')
  if (props.mode === 'advanced') values.value = await invoke('instancePreferences', { id: props.instanceId })
  if (props.mode === 'versions') components.value = await invoke('components', { id: props.instanceId })
  if (props.mode === 'worlds') worlds.value = await invoke('worlds', { id: props.instanceId })
  if (props.mode === 'notes') {
    const state = await invoke<{ instances: { id: string; notes: string }[] }>('snapshot')
    notes.value = state.instances.find(i => i.id === props.instanceId)?.notes || ''
  }
}
onMounted(() => run(refresh))
watch(() => props.mode, () => run(refresh))
async function loadVersions() {
  await invoke('loadComponentVersions', { id: props.instanceId, component: loader.value })
  versions.value = await invoke('componentVersions', { id: props.instanceId, component: loader.value })
  version.value = ''
  if (!versions.value.length) message.value = t('flows.metadataLoading')
}
</script>
<template>
  <section :aria-busy="busy">
    <p v-if="error" role="alert" class="workflow-error">{{ error }}</p>
    <p v-if="message" role="status">{{ message }}</p>
    <template v-if="mode === 'copy'">
      <p>{{ t('flows.create_an_independent_copy_with') }}</p>
      <label>{{ t('worlds.copy_name') }}<input v-model="name" maxlength="120"></label>
      <button class="setting-toggle" role="switch" :aria-checked="includeWorlds" @click="includeWorlds = !includeWorlds"><span class="setting-text"><span>{{ t('flows.includeWorlds') }}</span></span><span class="switch" aria-hidden="true"><span /></span></button>
      <button class="primary" :disabled="busy || !name.trim()" @click="run(async () => { await invoke('copyInstance', { id: instanceId, name, includeWorlds }); message = t('flows.copyStarted') })">{{ t('flows.create_instance_copy') }}</button>
    </template>
    <template v-else-if="mode === 'worlds'">
      <p>{{ t('flows.copy_a_world_before_experimenting') }}</p>
      <button v-for="w in worlds" :key="w.id" class="row" :aria-pressed="world === w.id" @click="world = w.id"><span>{{ w.name || w.id }}<small>{{ w.mode }}</small></span><span>{{ w.lastPlayed }}</span></button>
      <p v-if="!worlds.length">{{ t('flows.no_worlds_found_refresh_after') }}</p>
      <label>{{ t('worlds.copy_name') }}<input v-model="name" maxlength="100"></label>
      <button :disabled="busy || !world || !name.trim()" @click="run(async () => { await invoke('copyWorld', { id: instanceId, world, name }); message = t('flows.worldCopyStarted') })">{{ t('flows.create_world_copy') }}</button>
      <button @click="run(refresh)">{{ t('flows.refresh_worlds') }}</button>
    </template>
    <template v-else-if="mode === 'notes'">
      <label>{{ t('flows.instance_notes') }}<textarea v-model="notes" rows="10" maxlength="100000" /></label>
      <button class="primary" :disabled="busy" @click="run(async () => { await invoke('saveNotes', { id: instanceId, notes }); message = t('flows.notesSaved') })">{{ t('flows.save_notes') }}</button>
    </template>
    <template v-else-if="mode === 'export'">
      <p>{{ t('flows.create_a_prism_zip_in') }}</p>
      <button class="setting-toggle" role="switch" :aria-checked="includeWorlds" @click="includeWorlds = !includeWorlds"><span class="setting-text"><span>{{ t('flows.includeWorlds') }}</span></span><span class="switch" aria-hidden="true"><span /></span></button>
      <p>{{ t('flows.configuration_can_contain_server_addresses') }}</p>
      <button class="primary" :disabled="busy" @click="run(async () => { const path = await invoke<string>('exportInstance', { id: instanceId, includeWorlds }); message = t('flows.exportStarted', { path }) })">{{ t('app.export_instance') }}</button>
    </template>
    <template v-else-if="mode === 'import'">
      <p>{{ t('flows.import_a_prism_zip_curseforge') }}</p>
      <label>{{ t('app.instance_name') }}<input v-model="name" maxlength="120"></label>
      <label>{{ t('flows.archive_path_or_https_url') }}<input v-model="source" :placeholder="t('flows.home_deck_downloads_pack_mrpack')"></label>
      <button @click="browsing = !browsing">{{ browsing ? t('flows.closeBrowser') : t('flows.browseDownloads') }}</button>
      <FilePicker v-if="browsing" @select="source = $event; browsing = false" />
      <p class="muted">{{ t('flows.imported_packs_may_contain_executable') }}</p>
      <button class="primary" :disabled="busy || !name.trim() || !source.trim()" @click="run(async () => { await invoke('importInstance', { name, source }); message = t('flows.importStarted') })">{{ t('flows.import_pack') }}</button>
    </template>
    <template v-else-if="mode === 'preferences' || mode === 'advanced'">
      <p>{{ t('flows.defaults_for_instances_that_do') }}</p>
      <SettingsPane :values="values" :instance="mode === 'advanced'" />
      <button class="primary" :disabled="busy" @click="run(async () => { await invoke(mode === 'advanced' ? 'saveInstancePreferences' : 'saveGlobalSettings', { id: instanceId, values }); message = t('flows.settingsSaved') })">{{ mode === 'advanced' ? t('flows.saveInstanceSettings') : t('flows.saveDefaults') }}</button>
    </template>
    <template v-else-if="mode === 'versions'">
      <div v-for="c in components" :key="c.id" class="row"><strong>{{ c.name }}</strong><span>{{ c.version }}</span></div>
      <p>{{ t('flows.changing_versions_can_make_mods') }}</p>
      <div class="actions"><button v-for="(title, id) in { 'net.minecraft': 'Minecraft', 'net.neoforged': 'NeoForge', 'net.minecraftforge': 'Forge', 'net.fabricmc.fabric-loader': 'Fabric', 'org.quiltmc.quilt-loader': 'Quilt' }" :key="id" :aria-pressed="loader === id" @click="loader = id; versions = []; version = ''">{{ title }}</button></div>
      <div class="actions"><button :disabled="busy" @click="run(loadVersions)">{{ t('flows.refresh_versions') }}</button><input v-model="search" :placeholder="t('flows.filter_versions')" :aria-label="t('flows.filter_versions')"></div>
      <div class="version-list"><button v-for="v in versions.filter(v => v.includes(search)).slice(0, 200)" :key="v" :aria-pressed="version === v" @click="version = v">{{ v }}</button></div>
      <button class="primary" :disabled="busy || !version" @click="run(async () => { await invoke('setComponentVersion', { id: instanceId, component: loader, version }); await refresh(); message = t('flows.versionSaved') })">{{ t('flows.apply', { version }) }}</button>
    </template>
  </section>
</template>
