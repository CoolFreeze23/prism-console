<script setup lang="ts">
import { t, type MessageKey } from './i18n'
// The Browse tab: Modrinth modpacks in one list; moving along it previews a pack on the right, A installs the
// newest release that fits the filters, X picks another release. Filters open a picker list.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { invoke, type ModpackState, type ModpackRelease } from './bridge'
import { beginTextEntry, useGamepad, useGamepadAction, useGamepadInnerNav } from './upstream/gamepad'
import PackArt from './PackArt.vue'
import ButtonGlyph from './ButtonGlyph.vue'
const props = defineProps<{ state: ModpackState; busy: boolean }>()
const emit = defineEmits<{ installed: []; error: [message: string] }>()
const gamepad = useGamepad()
const query = ref(props.state.query || ''), chosen = ref(''), focusedIndex = ref(0), installing = ref('')
// Filters: one loader, one Minecraft release, a sort order ('' = popular without a query, best match with one).
const loader = ref(props.state.filters?.loader || ''), gameVersion = ref(props.state.filters?.gameVersion || ''), sort = ref(props.state.filters?.sort || '')
const loaderNames: Record<string, string> = { fabric: 'Fabric', neoforge: 'NeoForge', forge: 'Forge', quilt: 'Quilt' }
const sorts = ['downloads', 'relevance', 'newest', 'updated'] as const
const shownSort = computed(() => sort.value || (query.value.trim() ? 'relevance' : 'downloads'))
const filtered = computed(() => !!(loader.value || gameVersion.value || sort.value))
const hits = computed(() => props.state.hits || [])
const pack = computed(() => hits.value.find(h => h.id === chosen.value))
const releases = computed(() => props.state.project === chosen.value ? props.state.versions || [] : [])
const compact = new Intl.NumberFormat(undefined, { notation: 'compact', maximumFractionDigits: 1 })
const icons = ref<Record<string, string>>({})
watch(hits, async list => {
  const missing = list.filter(h => h.hasIcon && !(h.id in icons.value)).map(h => h.id)
  if (!missing.length) return
  try { icons.value = { ...icons.value, ...await invoke<Record<string, string>>('modpackIcons', { ids: missing }) } } catch { /* initials instead */ }
}, { immediate: true })
function call(method: string, args: Record<string, unknown> = {}) { return invoke(method, args).catch(e => emit('error', String(e))) }
function search(offset = 0) { return call('searchModpacks', { query: query.value, offset, loader: loader.value, gameVersion: gameVersion.value, sort: sort.value }) }
function updated(iso?: string) {
  if (!iso) return ''
  const days = Math.floor((Date.now() - Date.parse(iso)) / 86400000)
  return days <= 0 ? t('modpacks.updatedToday') : days === 1 ? t('modpacks.updatedYesterday') : days < 60 ? t('modpacks.updatedDaysAgo', { days }) : t('modpacks.updatedOn', { date: new Date(iso).toLocaleDateString() })
}

// The newest release that fits the filters (a full release when there is one).
const fitting = computed(() => releases.value.filter(r => (!gameVersion.value || r.minecraft.includes(gameVersion.value)) && (!loader.value || r.loaders.includes(loader.value))))
const release = ref('')
const recommended = computed(() => fitting.value.find(r => r.type === 'release') || fitting.value[0] || releases.value[0])
const target = computed<ModpackRelease | undefined>(() => releases.value.find(r => r.id === release.value) || recommended.value)
watch(chosen, () => { release.value = '' })
function describe(r?: ModpackRelease) {
  if (!r) return ''
  const loaders = r.loaders.map(l => loaderNames[l] || l).join(', ')
  const type = r.type === 'beta' || r.type === 'alpha' ? t(`modpacks.type.${r.type}` as MessageKey) : ''
  return [loaders, r.minecraft.length ? t('app.minecraftVersion', { version: r.minecraft[r.minecraft.length - 1] }) : '', type].filter(Boolean).join(' · ')
}

// Moving onto a pack previews it; its releases load once the player rests on it. A tap focuses before it
// clicks, so focus from a pointer leaves the choice to the click.
let previewTimer: number | undefined, pointerAt = 0
function focused(id: string, index: number) {
  focusedIndex.value = index
  if (Date.now() - pointerAt > 400) preview(id, index)
}
function preview(id: string, index: number) {
  focusedIndex.value = index
  if (id === chosen.value) return
  chosen.value = id
  window.clearTimeout(previewTimer)
  previewTimer = window.setTimeout(() => { if (chosen.value === id) call('modpackVersions', { project: id }) }, 220)
  if (index >= hits.value.length - 3 && hits.value.length < (props.state.total || 0) && !props.state.loading) search(hits.value.length)
}
// A (or a tap on the previewed pack) installs it; a tap on another pack previews that one first.
function activate(id: string, index: number) {
  if (id !== chosen.value) { preview(id, index); return }
  install()
}
async function install() {
  const p = pack.value
  if (!p || props.busy) return
  if (!target.value) {  // releases still loading: install as soon as they arrive
    installing.value = p.id
    if (props.state.project !== p.id && !props.state.versionsLoading) call('modpackVersions', { project: p.id })
    return
  }
  installing.value = ''
  try { await invoke('installModpack', { project: p.id, version: target.value.id, name: p.title }); emit('installed') }
  catch (e) { emit('error', String(e)) }
}
watch(target, value => { if (value && installing.value === chosen.value) install() })

// Pickers: a filter's values, or this pack's releases.
type Option = { value: string; label: string; detail?: string }
const picker = ref<{ kind: 'loader' | 'version' | 'sort' | 'release'; options: Option[]; current: string; from: HTMLElement | null } | null>(null)
const pickerRoot = ref<HTMLElement | null>(null)
function openPicker(kind: 'loader' | 'version' | 'sort' | 'release', event?: Event) {
  const from = (event?.currentTarget as HTMLElement) || (document.activeElement as HTMLElement)
  const options: Option[] = kind === 'loader' ? [{ value: '', label: t('modpacks.filter.anyLoader') }, ...Object.entries(loaderNames).map(([value, label]) => ({ value, label }))]
    : kind === 'version' ? [{ value: '', label: t('modpacks.filter.anyVersion') }, ...(props.state.gameVersions || []).map(v => ({ value: v, label: v }))]
    : kind === 'sort' ? sorts.map(value => ({ value, label: t(`modpacks.sort.${value}` as MessageKey) }))
    : releases.value.map(r => ({ value: r.id, label: r.number || r.name, detail: describe(r) }))
  const current = kind === 'loader' ? loader.value : kind === 'version' ? gameVersion.value : kind === 'sort' ? shownSort.value : target.value?.id || ''
  picker.value = { kind, options, current, from }
  gamepad.registerContext('prism-picker', { root: () => pickerRoot.value, onCancel: closePicker, onConfirm: () => (document.activeElement as HTMLElement)?.click() })
  nextTick(() => (pickerRoot.value?.querySelector<HTMLElement>('[aria-checked=true]') || pickerRoot.value?.querySelector<HTMLElement>('button'))?.focus({ preventScroll: false }))
}
function closePicker() {
  const from = picker.value?.from
  picker.value = null
  gamepad.unregisterContext('prism-picker')
  nextTick(() => from?.focus())
}
function pick(value: string) {
  const kind = picker.value?.kind
  closePicker()
  if (kind === 'release') { release.value = value; return }
  if (kind === 'loader') loader.value = value
  if (kind === 'version') gameVersion.value = value
  if (kind === 'sort') sort.value = value
  chosen.value = ''; focusedIndex.value = 0
  search()
}
function clearFilters() { loader.value = ''; gameVersion.value = ''; sort.value = ''; chosen.value = ''; search() }

// B closes a picker before it leaves the Browse tab.
function back(event: Event) { if (picker.value) { event.preventDefault(); closePicker() } }
onMounted(() => window.addEventListener('prism-back', back))
onUnmounted(() => { window.removeEventListener('prism-back', back); gamepad.unregisterContext('prism-picker') })
const searchField = ref<HTMLInputElement | null>(null)
useGamepadAction('Y', { label: () => t('modpacks.search'), handler: () => { if (searchField.value) beginTextEntry(searchField.value) }, disabled: () => !!picker.value })
useGamepadAction('X', { label: () => t('modpacks.otherVersion'), handler: () => { if (releases.value.length) openPicker('release') }, disabled: () => !!picker.value || !releases.value.length })
useGamepadInnerNav({ handler: direction => { if (direction === 'prev') document.querySelector<HTMLElement>('[data-filter=loader]')?.focus() }, disabled: () => !!picker.value })

onMounted(() => { if (!hits.value.length) search() })
// The page opens before its first results arrive. When they do, a controller starts on the first one, unless the
// player has already moved somewhere other than the search row.
let placedFocus = false
watch(hits, async list => {
  if (placedFocus || !list.length) return
  placedFocus = true
  await nextTick()
  const active = document.activeElement
  if (!active || active === document.body || active.closest('.modpack-search')) document.querySelector<HTMLElement>('.modpack-card')?.focus()
}, { immediate: true })
</script>

<template>
  <div class="modpacks">
    <div class="modpack-bar">
      <form class="modpack-search" @submit.prevent="chosen = ''; search()">
        <svg aria-hidden="true" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="square"><circle cx="11" cy="11" r="7" /><path d="M20 20l-4-4" /></svg>
        <input ref="searchField" v-model="query" type="search" :placeholder="t('modpacks.search_modrinth_modpacks')" :aria-label="t('modpacks.search_modrinth_modpacks')">
      </form>
      <button class="filter-field" data-filter="loader" @click="openPicker('loader', $event)"><span>{{ t('modpacks.filter.loaderShort') }}</span>{{ loader ? loaderNames[loader] : t('modpacks.filter.any') }}<span aria-hidden="true" class="caret">▾</span></button>
      <button class="filter-field" data-filter="version" @click="openPicker('version', $event)"><span>{{ t('modpacks.filter.versionShort') }}</span>{{ gameVersion || t('modpacks.filter.any') }}<span aria-hidden="true" class="caret">▾</span></button>
      <button class="filter-field" data-filter="sort" @click="openPicker('sort', $event)"><span>{{ t('modpacks.filter.sort') }}</span>{{ t(`modpacks.sort.${shownSort}` as MessageKey) }}<span aria-hidden="true" class="caret">▾</span></button>
      <span class="modpack-count" aria-live="polite">{{ hits.length ? t('modpacks.position', { index: focusedIndex + 1, total: compact.format(state.total || hits.length) }) : '' }}</span>
    </div>

    <div class="modpack-body">
      <section class="modpack-results" :aria-label="t('modpacks.results')">
        <div v-if="state.error" role="alert" class="modpack-error"><span>{{ state.error }}</span><button data-autofocus="true" @click="search(hits.length && state.query === query ? hits.length : 0)">{{ t('launch.tryAgain') }}</button></div>
        <div v-else-if="!hits.length" class="modpack-empty" role="status">
          <p>{{ state.loading ? t('modpacks.loadingList') : filtered ? t('modpacks.noneFiltered') : t('modpacks.none') }}</p>
          <button v-if="!state.loading && filtered" data-action="clear-filters" data-autofocus="true" @click="clearFilters">{{ t('modpacks.filter.reset') }}</button>
        </div>
        <button v-for="(hit, index) in hits" :key="hit.id" class="modpack-card" :class="{ chosen: hit.id === chosen }" :data-id="hit.id" :data-autofocus="index === 0 || undefined" @pointerdown="pointerAt = Date.now()" @focus="focused(hit.id, index)" @click="activate(hit.id, index)">
          <PackArt :src="icons[hit.id]" :name="hit.title" :size="80" />
          <span class="modpack-card-text">
            <strong>{{ hit.title }}</strong>
            <span class="modpack-blurb">{{ hit.description }}</span>
          </span>
          <span class="modpack-card-meta"><span>{{ t('modpacks.downloads', { count: compact.format(hit.downloads) }) }}</span><span>{{ updated(hit.updated) }}</span></span>
        </button>
        <button v-if="hits.length && hits.length < (state.total || 0)" class="modpack-more" data-action="load-more" :disabled="state.loading" @click="search(hits.length)">{{ state.loading ? t('modpacks.loading') : t('modpacks.loadMore') }}</button>
      </section>

      <aside v-if="pack" class="modpack-preview" :aria-label="pack.title">
        <div class="preview-art"><PackArt :src="icons[pack.id]" :name="pack.title" :size="176" /></div>
        <h2>{{ pack.title }}</h2>
        <span class="preview-byline">{{ t('modpacks.byAuthor', { author: pack.author, downloads: compact.format(pack.downloads) }) }}</span>
        <p class="preview-description">{{ pack.description }}</p>
        <div class="preview-install">
          <span v-if="state.versionsLoading && !releases.length" role="status">{{ t('modpacks.loading_releases') }}</span>
          <span v-else-if="!target" role="status">{{ t('modpacks.this_modpack_has_no_releases') }}</span>
          <span v-else class="preview-target">{{ t('modpacks.installs', { version: target.number || target.name }) }} <span>· {{ describe(target) }}</span></span>
          <div class="preview-actions" data-gamepad-skip>
            <button tabindex="-1" class="hero-action primary-action" data-action="install-modpack" :disabled="busy || (!target && !state.versionsLoading)" @click="install"><ButtonGlyph button="A" accent large />{{ installing === pack.id ? t('modpacks.installingSoon') : t('modpacks.installShort') }}</button>
            <button tabindex="-1" class="hero-action" data-action="other-version" :disabled="!releases.length" @click="openPicker('release', $event)"><ButtonGlyph button="X" />{{ t('modpacks.otherVersion') }}</button>
          </div>
        </div>
      </aside>
    </div>

    <div v-if="picker" ref="pickerRoot" class="modpack-picker" role="dialog" :aria-label="t(picker.kind === 'release' ? 'modpacks.release' : picker.kind === 'loader' ? 'modpacks.filter.loader' : picker.kind === 'version' ? 'modpacks.filter.version' : 'modpacks.filter.sort')">
      <h3>{{ t(picker.kind === 'release' ? 'modpacks.release' : picker.kind === 'loader' ? 'modpacks.filter.loader' : picker.kind === 'version' ? 'modpacks.filter.version' : 'modpacks.filter.sort') }}</h3>
      <div class="picker-options" role="radiogroup">
        <button v-for="option in picker.options" :key="option.value" role="radio" :aria-checked="option.value === picker.current" :data-value="option.value" @click="pick(option.value)">
          <span>{{ option.label }}</span><small v-if="option.detail">{{ option.detail }}</small>
        </button>
      </div>
    </div>
  </div>
</template>

<style>
.modpacks { position: relative; display: flex; flex-direction: column; gap: 12px; height: 100%; }
.modpack-bar { display: flex; align-items: center; gap: 12px; padding-top: 4px; }
.modpack-search { width: 312px; flex: none; display: flex; align-items: center; gap: 10px; height: 48px; padding: 0 14px; background: var(--pc-surface); box-shadow: inset 0 0 0 1px var(--pc-line); border-radius: var(--pc-radius); color: var(--pc-text-3); }
.modpack-search input { flex: 1; min-width: 0; min-height: 44px; padding: 0; background: transparent; box-shadow: none; font-size: 17px; }
.modpack-search input:focus-visible { outline-offset: 8px; }
.filter-field { display: inline-flex; align-items: center; gap: 8px; height: 48px; padding: 0 14px; background: var(--pc-surface); box-shadow: inset 0 0 0 1px var(--pc-line); font-size: 16px; white-space: nowrap; }
.filter-field > span:first-child { color: var(--pc-text-3); font-weight: 500; }
.filter-field .caret { color: var(--pc-text-3); font-size: 12px; }
.modpack-count { margin-left: auto; color: var(--pc-text-3); font-size: 15px; font-variant-numeric: tabular-nums; white-space: nowrap; }
.modpack-body { flex: 1; min-height: 0; display: flex; gap: 32px; }
.modpack-results { width: 760px; flex: none; overflow-y: auto; display: flex; flex-direction: column; gap: 8px; padding: 8px 8px 24px; margin: -8px -8px 0; scrollbar-width: none; }
.modpack-card { flex: none; display: flex; align-items: center; gap: 18px; height: 96px; padding: 0 16px 0 8px; background: var(--pc-surface); text-align: left; }
.modpack-card:hover:not(:disabled) { background: var(--pc-raised); }
.modpack-card.chosen { background: var(--pc-raised); }
.modpack-card-text { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 4px; }
.modpack-card-text strong { font-size: 20px; font-weight: 650; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.modpack-blurb { font-size: 16px; color: var(--pc-text-2); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.modpack-card-meta { flex: none; display: flex; flex-direction: column; align-items: flex-end; gap: 4px; font-size: 15px; color: var(--pc-text-3); font-variant-numeric: tabular-nums; font-weight: 500; }
.modpack-more { flex: none; width: 100%; }
.modpack-error { display: flex; align-items: center; gap: 16px; padding: 12px 16px; background: var(--pc-surface); box-shadow: inset 3px 0 0 var(--pc-danger); }
.modpack-error span { flex: 1; }
.modpack-empty { padding: 24px 0; display: flex; flex-direction: column; align-items: flex-start; gap: 12px; font-size: 18px; color: var(--pc-text-2); }
.modpack-empty p { margin: 0; }
.modpack-preview { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 10px; overflow: hidden; }
.preview-art { height: 200px; display: grid; place-items: center; background: var(--pc-surface); }
.modpack-preview h2 { margin: 6px 0 0; font-size: 28px; overflow-wrap: anywhere; }
.preview-byline { font-size: 16px; color: var(--pc-text-2); }
.preview-description { margin: 0; font-size: 17px; line-height: 1.45; display: -webkit-box; -webkit-line-clamp: 4; -webkit-box-orient: vertical; overflow: hidden; }
.preview-install { margin-top: auto; padding: 16px; background: var(--pc-surface); display: flex; flex-direction: column; gap: 12px; font-size: 16px; color: var(--pc-text-2); }
.preview-target { color: var(--pc-text); font-weight: 650; font-variant-numeric: tabular-nums; }
.preview-target span { color: var(--pc-text-2); font-weight: 500; }
.preview-actions { display: flex; flex-wrap: wrap; gap: 8px 24px; align-items: center; }
.modpack-picker { position: absolute; top: 60px; right: 0; bottom: 0; width: 408px; z-index: 5; display: flex; flex-direction: column; gap: 8px; padding: 16px; background: var(--pc-raised); box-shadow: -1px 0 0 var(--pc-line); }
.modpack-picker h3 { margin: 0 0 4px; font-size: 20px; }
.picker-options { flex: 1; min-height: 0; overflow-y: auto; display: flex; flex-direction: column; gap: 6px; padding: 6px; margin: -6px; }
.picker-options button { flex: none; display: flex; flex-direction: column; align-items: flex-start; gap: 2px; text-align: left; background: var(--pc-surface); font-size: 17px; }
.picker-options button[aria-checked=true] { box-shadow: inset 3px 0 0 var(--pc-text); }
.picker-options small { font-size: 15px; color: var(--pc-text-2); font-weight: 500; }
@media (max-width: 900px) {
  .modpack-bar { flex-wrap: wrap; }
  .modpack-search { width: 100%; }
  .modpack-body { flex-direction: column; }
  .modpack-results { width: auto; }
  .modpack-picker { width: 100%; }
}
</style>
