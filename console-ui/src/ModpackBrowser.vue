<script setup lang="ts">
import { t } from './i18n'
// Native Modrinth modpack browser: search on the left, the chosen pack and its releases on the right.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { invoke, type ModpackState } from './bridge'
import fallbackIcon from './fallback.svg'
const props = defineProps<{ state: ModpackState; busy: boolean }>()
const emit = defineEmits<{ installed: []; error: [message: string] }>()
const query = ref(''), chosen = ref(''), release = ref(''), name = ref('')
const hits = computed(() => props.state.hits || [])
const pack = computed(() => hits.value.find(h => h.id === chosen.value))
const releases = computed(() => props.state.project === chosen.value ? props.state.versions || [] : [])
const compact = new Intl.NumberFormat(undefined, { notation: 'compact', maximumFractionDigits: 1 })
// Icons arrive once each and are kept here, so the polled state never carries image data.
const icons = ref<Record<string, string>>({})
watch(hits, async list => {
  const missing = list.filter(h => h.hasIcon && !(h.id in icons.value)).map(h => h.id)
  if (!missing.length) return
  try { icons.value = { ...icons.value, ...await invoke<Record<string, string>>('modpackIcons', { ids: missing }) } } catch { /* fallback icon */ }
}, { immediate: true })
const noReleases = computed(() => !!chosen.value && props.state.project === chosen.value && !props.state.versionsLoading && !props.state.error && !releases.value.length)
function retry() { if (chosen.value && props.state.project === chosen.value && !releases.value.length) call('modpackVersions', { project: chosen.value }); else search(hits.value.length && props.state.query === query.value ? hits.value.length : 0) }
// B steps back from a chosen pack to the results before it leaves the browser.
function back(event: Event) {
  if (!chosen.value) return
  event.preventDefault()
  const id = chosen.value
  chosen.value = ''
  nextTick(() => document.querySelector<HTMLElement>(`.modpack-card[data-id="${id}"]`)?.focus())
}
onMounted(() => window.addEventListener('prism-back', back))
onUnmounted(() => window.removeEventListener('prism-back', back))
function call(method: string, args: Record<string, unknown> = {}) { return invoke(method, args).catch(e => emit('error', String(e))) }
function search(offset = 0) { return call('searchModpacks', { query: query.value, offset }) }
function choose(id: string) {
  chosen.value = id; release.value = ''
  name.value = hits.value.find(h => h.id === id)?.title || ''
  call('modpackVersions', { project: id })
}
// Default to the newest full release once the list arrives.
watch(releases, list => { if (!release.value && list.length) release.value = (list.find(r => r.type === 'release') || list[0]).id })
async function install() {
  try { await invoke('installModpack', { project: chosen.value, version: release.value, name: name.value }); emit('installed') }
  catch (e) { emit('error', String(e)) }
}
function describe(r: { minecraft: string[]; loaders: string[]; type: string }) {
  const loader = r.loaders.map(l => l.charAt(0).toUpperCase() + l.slice(1)).join(', ')
  const type = r.type === 'beta' || r.type === 'alpha' ? t(`modpacks.type.${r.type}`) : ''
  return [r.minecraft.length ? t('app.minecraftVersion', { version: r.minecraft[r.minecraft.length - 1] }) : '', loader, type].filter(Boolean).join(' · ')
}
onMounted(() => { if (!hits.value.length) search() })
</script>

<template>
  <div class="modpacks">
    <section class="modpack-results">
      <form class="modpack-search" @submit.prevent="search()">
        <input v-model="query" type="search" :placeholder="t('modpacks.search_modrinth_modpacks')" :aria-label="t('modpacks.search_modrinth_modpacks')">
        <button type="submit" :disabled="state.loading">{{ t('modpacks.search') }}</button>
      </form>
      <div v-if="state.error" role="alert" class="modpack-error"><span>{{ state.error }}</span><button @click="retry">{{ t('launch.tryAgain') }}</button></div>
      <p v-if="!hits.length" class="muted" role="status">{{ state.loading ? t('modpacks.loadingList') : t('modpacks.none') }}</p>
      <div class="modpack-grid">
        <button v-for="hit in hits" :key="hit.id" class="modpack-card" :data-id="hit.id" :aria-pressed="hit.id === chosen" @click="choose(hit.id)">
          <img :src="icons[hit.id] || fallbackIcon" alt="" loading="lazy">
          <span class="modpack-card-text">
            <strong>{{ hit.title }}</strong>
            <small>{{ t('modpacks.byline', { author: hit.author, downloads: compact.format(hit.downloads) }) }}<template v-if="hit.minecraft"> · {{ hit.minecraft }}</template></small>
            <span class="modpack-blurb">{{ hit.description }}</span>
          </span>
        </button>
      </div>
      <button v-if="hits.length && hits.length < (state.total || 0)" class="modpack-more" data-action="load-more" :disabled="state.loading" @click="search(hits.length)">{{ state.loading ? t('modpacks.loading') : t('modpacks.loadMore') }}</button>
    </section>
    <aside v-if="pack" class="modpack-detail" :aria-label="t('modpacks.selected_modpack')">
      <img :src="icons[pack.id] || fallbackIcon" alt="" class="modpack-detail-icon">
      <h2>{{ pack.title }}</h2>
      <p class="muted">{{ t('modpacks.byAuthor', { author: pack.author, downloads: compact.format(pack.downloads) }) }}</p>
      <p>{{ pack.description }}</p>
      <div v-if="pack.categories?.length" class="bp-hero-chips"><span v-for="c in pack.categories" :key="c" class="bp-chip chip-quiet">{{ c }}</span></div>
      <h3 class="section-title">{{ t('modpacks.release') }}</h3>
      <p v-if="state.versionsLoading" class="muted" role="status">{{ t('modpacks.loading_releases') }}</p>
      <p v-if="noReleases" class="muted" role="status">{{ t('modpacks.this_modpack_has_no_releases') }}</p>
      <div class="modpack-releases" role="radiogroup" :aria-label="t('modpacks.release')">
        <button v-for="r in releases" :key="r.id" role="radio" :aria-checked="r.id === release" @click="release = r.id">
          <strong>{{ r.number || r.name }}</strong><small>{{ describe(r) }}</small>
        </button>
      </div>
      <label class="setting-field"><span>{{ t('app.instance_name') }}</span><input v-model="name" maxlength="120"></label>
      <button class="primary modpack-install" :disabled="busy || !release || !name.trim()" data-action="install-modpack" @click="install">{{ t('modpacks.install', { name: pack.title }) }}</button>
    </aside>
  </div>
</template>

<style>
.modpacks { display: grid; grid-template-columns: minmax(0, 1fr) minmax(320px, 38%); gap: 28px; align-items: start; }
.modpack-search { display: flex; gap: 12px; margin-bottom: 16px; }
.modpack-search input { flex: 1; }
.modpack-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 12px; }
.modpack-card { display: flex; gap: 14px; align-items: flex-start; text-align: left; padding: 14px; min-height: 112px; }
.modpack-card img { width: 64px; height: 64px; flex: none; image-rendering: auto; }
.modpack-card-text { display: grid; gap: 4px; min-width: 0; }
.modpack-card-text strong { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.modpack-card small, .modpack-releases small { color: var(--ore-muted); font-size: 12px; }
.modpack-blurb { font-size: 13px; color: #d6d8da; display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden; }
.modpack-card[aria-pressed=true] { background: #253321; border-image-source: url('./oreui/button-accent-default.svg'); }
.modpack-more { margin-top: 16px; width: 100%; }
.modpack-error { display: flex; align-items: center; gap: 16px; padding: 12px 16px; margin-bottom: 16px; background: #3a1d1d; border: 2px solid #b54b4b; }
.modpack-error span { flex: 1; }
.modpack-detail { position: sticky; top: 0; max-height: calc(100vh - 200px); overflow: auto; background: var(--ore-panel); padding: 20px 24px; border: 2px solid #39393b; }
.modpack-detail .setting-field { grid-template-columns: 1fr; gap: 6px; padding: 6px 0; border: 0; }
.modpack-detail-icon { float: left; margin: 0 16px 8px 0; }
.modpack-detail-icon { width: 80px; height: 80px; }
.modpack-detail h2 { margin: 4px 0; }
.modpack-releases { display: grid; gap: 8px; max-height: 22vh; overflow: auto; padding: 4px; margin-bottom: 8px; clear: both; }
.modpack-releases button { display: grid; gap: 2px; text-align: left; min-height: 56px; }
.modpack-releases button[aria-checked=true] { background: var(--ore-accent); border-image-source: url('./oreui/button-accent-default.svg'); }
.modpack-releases button[aria-checked=true] small { color: #e8f5e3; }
.modpack-install { width: 100%; margin-top: 16px; }
@media (max-width: 900px) { .modpacks { grid-template-columns: 1fr; } .modpack-detail { position: static; } }
</style>
