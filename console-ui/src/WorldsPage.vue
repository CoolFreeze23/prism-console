<script setup lang="ts">
import { t } from './i18n'
// Worlds (play straight into one, or copy it) and the instance's screenshot gallery.
import { computed, nextTick, onMounted, onUnmounted, ref } from 'vue'
import { invoke } from './bridge'
import fallbackIcon from './fallback.svg'
type World = { id: string; name: string; mode: string; lastPlayedMs: number; size: number; icon: string }
type Shot = { name: string; taken: number; thumb: string }
type Server = { name: string; address: string; icon: string }
const props = defineProps<{ instanceId: string; running: boolean; busy: boolean }>()
const emit = defineEmits<{ play: [target: { world: string } | { server: string }]; error: [message: string] }>()
const tab = ref<'worlds' | 'servers' | 'screenshots'>('worlds')
// Servers are picked by position: the list may hold the same address twice.
const servers = ref<Server[]>([]), serversLoaded = ref(false), chosenServer = ref(-1)
const server = computed(() => servers.value[chosenServer.value])
const editing = ref<{ index: number; expected: string; name: string; address: string } | null>(null), removing = ref(false)
const worlds = ref<World[]>([]), chosen = ref(''), copyName = ref(''), loaded = ref(false)
const shots = ref<Shot[]>([]), totalShots = ref(0), viewing = ref<{ name: string; image: string } | null>(null)
const recentFirst = computed(() => [...worlds.value].sort((a, b) => b.lastPlayedMs - a.lastPlayedMs))
const world = computed(() => worlds.value.find(w => w.id === chosen.value))
function call<T>(method: string, args: Record<string, unknown> = {}) { return invoke<T>(method, { id: props.instanceId, ...args }) }
async function guard(job: () => Promise<unknown>) { try { await job() } catch (e) { emit('error', String(e)) } }
function when(ms: number) {
  if (!ms) return t('worlds.neverPlayed')
  const days = Math.floor((new Date().setHours(0, 0, 0, 0) - new Date(ms).setHours(0, 0, 0, 0)) / 86400000)
  return days <= 0 ? t('app.playedToday') : days === 1 ? t('app.playedYesterday') : days < 7 ? t('app.playedDaysAgo', { days }) : t('app.playedOn', { date: new Date(ms).toLocaleDateString() })
}
const size = (bytes: number) => bytes > 1e9 ? `${(bytes / 1e9).toFixed(1)} GB` : `${Math.max(1, Math.round(bytes / 1e6))} MB`
function loadWorlds() { return guard(async () => { worlds.value = await call<World[]>('worlds'); loaded.value = true; if (!chosen.value) chosen.value = recentFirst.value[0]?.id || '' }) }
function loadShots(offset = 0) {
  return guard(async () => {
    const page = await call<{ total: number; shots: Shot[] }>('screenshots', { offset })
    shots.value = offset ? [...shots.value, ...page.shots] : page.shots
    totalShots.value = page.total
  })
}
function loadServers() { return guard(async () => { servers.value = await call<Server[]>('servers'); serversLoaded.value = true; if (!server.value) chosenServer.value = servers.value.length ? 0 : -1 }) }
async function focusIn(selector: string) { await nextTick(); document.querySelector<HTMLElement>(selector)?.focus() }
function editServer(index: number) {
  const current = servers.value[index]
  editing.value = current ? { index, expected: current.address, name: current.name, address: current.address } : { index: -1, expected: '', name: '', address: '' }
  removing.value = false
  focusIn('.server-form input')
}
function stopEditing() { const wasNew = editing.value?.index === -1; editing.value = null; removing.value = false; focusIn(wasNew ? '[data-action=add-server]' : '.world-detail button') }
function saveServer() {
  const form = editing.value
  if (!form) return
  guard(async () => {
    servers.value = await call<Server[]>('saveServer', { index: form.index, expected: form.expected, name: form.name.trim(), address: form.address.trim() })
    chosenServer.value = form.index >= 0 ? form.index : servers.value.length - 1
    editing.value = null
    focusIn('.world-list [aria-pressed=true]')
  })
}
function removeServer() {
  const index = chosenServer.value, current = server.value
  if (!current) return
  guard(async () => {
    servers.value = await call<Server[]>('removeServer', { index, expected: current.address })
    removing.value = false
    chosenServer.value = Math.min(index, servers.value.length - 1)
    focusIn(servers.value.length ? '.world-list [aria-pressed=true]' : '[data-action=add-server]')
  })
}
function show(tabName: 'worlds' | 'servers' | 'screenshots') {
  tab.value = tabName
  if (tabName === 'screenshots' && !shots.value.length) loadShots()
  if (tabName === 'servers' && !serversLoaded.value) loadServers()
}
function open(name: string) {
  guard(async () => {
    viewing.value = await call<{ name: string; image: string }>('screenshot', { name })
    await nextTick(); document.querySelector<HTMLElement>('.shot-viewer button')?.focus()
  })
}
function closeViewer() {
  const name = viewing.value?.name
  viewing.value = null
  nextTick(() => document.querySelector<HTMLElement>(`.shot[data-name="${name}"]`)?.focus())
}
// B closes an open screenshot, a server form or a removal question before it leaves the page.
function back(event: Event) {
  if (viewing.value) { event.preventDefault(); closeViewer() }
  else if (editing.value || removing.value) { event.preventDefault(); stopEditing() }
}
onMounted(() => window.addEventListener('prism-back', back))
onUnmounted(() => window.removeEventListener('prism-back', back))
function copy() { guard(async () => { await call('copyWorld', { world: chosen.value, name: copyName.value.trim() }); copyName.value = ''; await loadWorlds() }) }
onMounted(loadWorlds)
</script>

<template>
  <div class="worlds-page">
    <div class="control-segmented" role="tablist" :aria-label="t('worlds.worlds_and_screenshots')">
      <button role="tab" :aria-selected="tab === 'worlds'" :aria-checked="tab === 'worlds'" @click="show('worlds')">{{ t('app.worlds') }}</button>
      <button role="tab" :aria-selected="tab === 'servers'" :aria-checked="tab === 'servers'" @click="show('servers')">{{ t('worlds.servers') }}</button>
      <button role="tab" :aria-selected="tab === 'screenshots'" :aria-checked="tab === 'screenshots'" @click="show('screenshots')">{{ t('worlds.screenshots') }}</button>
    </div>

    <div v-if="tab === 'worlds'" class="worlds-layout">
      <div class="world-list">
        <p v-if="loaded && !worlds.length" class="muted">{{ t('worlds.no_worlds_yet_they_appear') }}</p>
        <button v-for="w in recentFirst" :key="w.id" class="world-row" :data-world="w.id" :aria-pressed="w.id === chosen" @click="chosen = w.id">
          <img :src="w.icon || fallbackIcon" alt="">
          <span class="setting-text"><span>{{ w.name || w.id }}</span><small>{{ w.mode }} · {{ when(w.lastPlayedMs) }} · {{ size(w.size) }}</small></span>
        </button>
      </div>
      <aside v-if="world" class="world-detail" :aria-label="t('worlds.selected_world')">
        <img :src="world.icon || fallbackIcon" alt="" class="world-detail-icon">
        <h2>{{ world.name || world.id }}</h2>
        <p class="muted">{{ world.mode }} · {{ when(world.lastPlayedMs) }}</p>
        <button class="primary world-play" :disabled="busy || running" @click="emit('play', { world: world.id })">{{ t('worlds.play_this_world') }}</button>
        <p v-if="running" class="muted">{{ t('worlds.the_game_is_already_running') }}</p>
        <h3 class="section-title">{{ t('worlds.make_a_copy') }}</h3>
        <p class="muted">{{ t('worlds.a_safe_backup_before_trying') }}</p>
        <label class="setting-field"><span>{{ t('worlds.copy_name') }}</span><input v-model="copyName" maxlength="100" :placeholder="t('worlds.backupName', { world: world.id })"></label>
        <button :disabled="busy || running || !copyName.trim()" @click="copy">{{ t('worlds.copy_world') }}</button>
      </aside>
    </div>

    <div v-else-if="tab === 'servers'" class="worlds-layout">
      <div class="world-list">
        <p v-if="serversLoaded && !servers.length" class="muted">{{ t('worlds.no_servers_saved_yet') }}</p>
        <button v-for="(s, index) in servers" :key="index" class="world-row" :aria-pressed="index === chosenServer" :disabled="!!editing" @click="chosenServer = index; removing = false">
          <img :src="s.icon || fallbackIcon" alt="">
          <span class="setting-text"><span>{{ s.name || s.address }}</span><small>{{ s.address }}</small></span>
        </button>
        <button data-action="add-server" :disabled="busy || running || !!editing" @click="editServer(-1)">{{ t('servers.add') }}</button>
        <p v-if="running" class="muted">{{ t('servers.closeGameToEdit') }}</p>
      </div>
      <form v-if="editing" class="world-detail server-form" :aria-label="editing.index < 0 ? t('servers.add') : t('servers.edit')" @submit.prevent="saveServer">
        <h2>{{ editing.index < 0 ? t('servers.add') : t('servers.edit') }}</h2>
        <label class="setting-field"><span>{{ t('servers.name') }}</span><input v-model="editing.name" data-field="name" maxlength="64" :placeholder="t('servers.namePlaceholder')"></label>
        <label class="setting-field"><span>{{ t('servers.address') }}</span><input v-model="editing.address" data-field="address" maxlength="255" spellcheck="false" autocapitalize="off" placeholder="play.example.net"></label>
        <p class="muted">{{ t('servers.addressHint') }}</p>
        <div class="actions"><button type="submit" class="primary" data-action="save-server" :disabled="busy || !editing.name.trim() || !editing.address.trim()">{{ t('servers.save') }}</button><button type="button" @click="stopEditing">{{ t('servers.cancel') }}</button></div>
      </form>
      <aside v-else-if="server" class="world-detail" :aria-label="t('worlds.selected_server')">
        <img :src="server.icon || fallbackIcon" alt="" class="world-detail-icon">
        <h2>{{ server.name || server.address }}</h2>
        <p class="muted">{{ server.address }}</p>
        <template v-if="removing">
          <p>{{ t('servers.removeQuestion', { name: server.name || server.address }) }}</p>
          <div class="actions"><button data-action="confirm-remove-server" :disabled="busy || running" @click="removeServer">{{ t('servers.remove') }}</button><button @click="stopEditing">{{ t('servers.keep') }}</button></div>
        </template>
        <template v-else>
          <button class="primary world-play" :disabled="busy || running" @click="emit('play', { server: server.address })">{{ t('worlds.join_server') }}</button>
          <p v-if="running" class="muted">{{ t('worlds.the_game_is_already_running') }}</p>
          <div class="actions"><button data-action="edit-server" :disabled="busy || running" @click="editServer(chosenServer)">{{ t('servers.edit') }}</button><button data-action="remove-server" :disabled="busy || running" @click="removing = true; focusIn('[data-action=confirm-remove-server]')">{{ t('servers.remove') }}</button></div>
        </template>
      </aside>
    </div>

    <div v-else>
      <p v-if="!shots.length" class="muted">{{ t('worlds.no_screenshots_yet_press_f2') }}</p>
      <div class="shot-grid">
        <button v-for="s in shots" :key="s.name" :data-name="s.name" class="shot" :aria-label="t('worlds.screenshotLabel', { date: new Date(s.taken).toLocaleString() })" @click="open(s.name)">
          <img :src="s.thumb" alt=""><small>{{ new Date(s.taken).toLocaleDateString() }}</small>
        </button>
      </div>
      <button v-if="shots.length < totalShots" class="modpack-more" @click="loadShots(shots.length)">{{ t('worlds.loadMoreLeft', { count: totalShots - shots.length }) }}</button>
    </div>

    <div v-if="viewing" class="shot-viewer" role="dialog" :aria-label="t('worlds.screenshot')">
      <img :src="viewing.image" :alt="viewing.name">
      <button class="primary" @click="closeViewer">{{ t('worlds.back_to_gallery') }}</button>
    </div>
  </div>
</template>

<style>
.worlds-page .control-segmented { margin-bottom: 20px; }
.worlds-layout { display: grid; grid-template-columns: minmax(0, 1fr) minmax(300px, 36%); gap: 28px; align-items: start; }
.world-list { display: grid; gap: 10px; }
.world-row { display: flex; gap: 16px; align-items: center; text-align: left; min-height: 80px; border-radius: 16px; }
.world-row img { width: 52px; height: 52px; border-radius: 10px; image-rendering: pixelated; flex: none; }
.world-row[aria-pressed=true] { background: var(--pc-hover); box-shadow: inset 0 0 0 2px var(--pc-text-2); }
.world-detail { position: sticky; top: 0; background: var(--pc-surface); padding: 24px 28px; border-radius: 24px; }
.world-detail-icon { width: 104px; height: 104px; border-radius: 18px; image-rendering: pixelated; }
.world-detail h2 { margin: 12px 0 4px; overflow-wrap: anywhere; }
.world-play { width: 100%; margin: 12px 0 4px; min-height: 60px; font-size: 20px; font-weight: 700; border-radius: 16px; }
.server-form .actions { margin-top: 12px; }
.world-detail .setting-field { grid-template-columns: 1fr; gap: 6px; padding: 6px 0; border: 0; }
.shot-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 12px; }
.shot { display: grid; gap: 8px; padding: 10px; border-radius: 16px; }
.shot img { width: 100%; aspect-ratio: 16 / 10; object-fit: cover; border-radius: 10px; }
.shot small { color: var(--ore-muted); }
.shot-viewer { position: fixed; inset: 0; z-index: 300; background: #000e; display: grid; place-items: center; gap: 16px; padding: 24px; grid-template-rows: 1fr auto; }
.shot-viewer img { max-width: 100%; max-height: calc(100vh - 120px); object-fit: contain; }
@media (max-width: 900px) { .worlds-layout { grid-template-columns: 1fr; } .world-detail { position: static; } }
</style>
