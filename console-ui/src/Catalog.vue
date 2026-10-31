<script setup lang="ts">
import { t, type MessageKey } from './i18n'
import { ref, onMounted, onUnmounted } from 'vue'
import { invoke } from './bridge'
const props = defineProps<{ instanceId: string; kind: string }>()
const query = ref(''), project = ref(''), error = ref(''), busy = ref(false), offset = ref(0)
const data = ref<{ projects: { id: string; name: string }[]; versions: { id: string; name: string; file: string }[]; project?: string; plan?: { name: string; file: string }[] }>({ projects: [], versions: [] })
let timer: ReturnType<typeof setInterval> | undefined
async function refresh() { try { data.value = await invoke('catalogResults', { id: props.instanceId }) } catch (e) { error.value = String(e) } }
async function action(method: string, extra = {}) {
  if (busy.value) return
  busy.value = true; error.value = ''
  try { await invoke(method, { id: props.instanceId, kind: props.kind, project: project.value, query: query.value, offset: offset.value, ...extra }); await refresh() }
  catch (e) { error.value = String(e) } finally { busy.value = false }
}
onMounted(() => { refresh(); timer = setInterval(refresh, 2000) })
onUnmounted(() => clearInterval(timer))
</script>
<template>
  <section :aria-label="t('catalog.modrinth_catalog')">
    <p>{{ t('catalog.filtered', { kind: t(`app.kind.${kind}` as MessageKey) }) }}</p>
    <div class="actions"><input v-model="query" :aria-label="t('catalog.search_modrinth')" :placeholder="t('catalog.search_modrinth')" @keydown.enter="offset = 0; action('searchCatalog')"><button :disabled="busy" @click="offset = 0; action('searchCatalog')">{{ t('modpacks.search') }}</button></div>
    <p v-if="error" role="alert" class="workflow-error">{{ error }}</p>
    <div class="resource-list"><button v-for="p in data.projects" :key="p.id" class="row" :aria-pressed="project === p.id" :disabled="busy" @click="project = p.id; action('catalogVersions')">{{ p.name }}</button></div>
    <div class="actions"><button :disabled="busy || offset === 0" @click="offset = Math.max(0, offset - 20); action('searchCatalog')">{{ t('catalog.previous_results') }}</button><button :disabled="busy || !data.projects.length" @click="offset += 20; action('searchCatalog')">{{ t('catalog.more_results') }}</button></div>
    <div v-if="data.project === project && project" class="resource-list"><h3>{{ t('catalog.compatible_releases') }}</h3><button v-for="v in data.versions" :key="v.id" class="row" :disabled="busy" @click="action('installCatalogResource', { version: v.id })">{{ t('catalog.review', { name: v.name || v.file }) }}</button></div>
    <section v-if="data.plan?.length"><h3>{{ t('catalog.installation_review') }}</h3><p>{{ t('catalog.these_files_including_resolved_dependencies') }}</p><ul><li v-for="p in data.plan" :key="p.file">{{ p.name }} — {{ p.file }}</li></ul><button class="primary" :disabled="busy" @click="action('confirmCatalogInstall')">{{ t('catalog.install_these_files') }}</button></section>
  </section>
</template>
