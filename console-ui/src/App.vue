<script setup lang="ts">
// Layout adapted from XMCL PR #1790's AppBigPictureView / BigPictureLibrary.
// The service layer is replaced with Prism's in-process QWebChannel bridge.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import { connect, invoke, type Snapshot, type Resource, type ModpackState, type ModUpdateState } from './bridge'
import { beginTextEntry, isTextField, moveFocus, useGamepad, useGamepadAction } from './upstream/gamepad'
import { chooseLocale, locale, t, tn, type MessageKey } from './i18n'
import { en } from './messages/en'
import fallbackIcon from './fallback.svg'
import Workflows from './Workflows.vue'
import Catalog from './Catalog.vue'
import ModpackBrowser from './ModpackBrowser.vue'
import ModUpdates from './ModUpdates.vue'
import WorldsPage from './WorldsPage.vue'
import FilePicker from './FilePicker.vue'
import AccountPanel from './AccountPanel.vue'
import NativeWorkflow from './NativeWorkflow.vue'
import ButtonGlyph from './ButtonGlyph.vue'
import PackArt from './PackArt.vue'
import LauncherSettings from './LauncherSettings.vue'
import { play, soundSettings } from './sound'
import type { Control } from './WorkflowControl.vue'
function iconError(event: Event) { const image = event.target as HTMLImageElement; if (!image.src.endsWith(fallbackIcon)) image.src = fallbackIcon }
const state = ref<Snapshot>({ instances: [], accounts: [] })
const selectedId = ref(''), tab = ref('Library'), error = ref(''), busy = ref(false), connected = ref(false)
// Accounts is reached from the account chip, not the tab row.
const tabs = ['Library', 'Browse', 'Settings']
const selected = computed(() => state.value.instances.find(i => i.id === selectedId.value))
const account = computed(() => state.value.accounts.find(a => a.selected))
// Most recently played first, so the pack you were playing is always the first card.
const library = computed(() => [...state.value.instances].sort((a, b) => (b.lastLaunch || 0) - (a.lastLaunch || 0) || a.name.localeCompare(b.name)))
const recent = computed(() => library.value[0]?.lastLaunch ? library.value[0] : undefined)
function playTime(seconds = 0) {
  const hours = Math.floor(seconds / 3600), minutes = Math.floor(seconds % 3600 / 60)
  return hours ? t('app.playedFor', { time: t('time.hoursMinutes', { hours, minutes }) }) : minutes ? t('app.playedFor', { time: t('time.minutes', { minutes }) }) : ''
}
// The hero's two lines: when the pack was played, and what it runs on.
const playedLine = computed(() => selected.value ? [lastPlayed(selected.value.lastLaunch), playTime(selected.value.playTime)].filter(Boolean).join(' · ') : '')
const metaLine = computed(() => selected.value ? [selected.value.loader, selected.value.version ? t('app.minecraftVersion', { version: selected.value.version }) : t('app.versionNotInstalled')].filter(Boolean).join(' · ') : '')
// Moving onto a pack picks it (the hero follows); A or a tap on the picked pack plays it.
function focusTile(id: string) { if (Date.now() - lastPointer > 400) selectedId.value = id }
function tileClick(id: string) { if (id !== selectedId.value) selectedId.value = id; else playSelected() }
function playSelected() {
  const pack = selected.value
  if (!pack || busy.value) return
  if (pack.running) action('backToGame')
  else if (pack.canLaunch && !pack.launching) action('launch', { id: pack.id })
}
function lastPlayed(when = 0) {
  if (!when) return ''
  const days = Math.floor((new Date().setHours(0, 0, 0, 0) - new Date(when).setHours(0, 0, 0, 0)) / 86400000)
  return days <= 0 ? t('app.playedToday') : days === 1 ? t('app.playedYesterday') : days < 7 ? t('app.playedDaysAgo', { days }) : t('app.playedOn', { date: new Date(when).toLocaleDateString() })
}
const modal = ref(''), modalRoot = ref<HTMLElement | null>(null), resources = ref<Resource[]>([]), kind = ref('mods')
const name = ref(''), minMemory = ref(2048), maxMemory = ref(8192)
const newName = ref(''), newVersion = ref('')
const logLines = ref<string[]>([])
const modpacks = ref<ModpackState>({})
let modpacksRevision: number | undefined
const modUpdates = ref<ModUpdateState>({})
const promptInput = ref('')
const managedAccount = ref('')
const flow = ref<{ id: string; title: string; content: Control } | null>(null)
const nativePanel = ref<InstanceType<typeof NativeWorkflow> | null>(null)
const commands = ref<{ id: string; text: string; enabled: boolean; name: string }[]>([])
const commandFilter = ref('')
let beforeWorkflow = ''
watch(() => flow.value?.id, id => {
  if (id) { if (modal.value !== 'workflow') beforeWorkflow = modal.value; open('workflow') }
  else if (modal.value === 'workflow') { modal.value = beforeWorkflow; if (!modal.value) close() }
})
watch([() => state.value.setup?.required, () => state.value.prompt?.id], ([required, prompt]) => { if (required && !prompt) open('setup'); else if (!required && modal.value === 'setup') close() })
const localResource = ref(''), resourceBrowser = ref(false)
const dialogTitle = computed(() => modal.value === 'workflow' ? flow.value?.title || 'Prism' : state.value.prompt?.id ? tn(state.value.prompt.title) : (modal.value && `title.${modal.value}` in en ? t(`title.${modal.value}` as MessageKey) : t('title.activity')))
// Short confirmations stay as a centred card; everything else is a full-screen page.
const compact = computed(() => ['prompt', 'stop', 'cancel-launch'].includes(modal.value))
watch(() => state.value.prompt?.id, id => {
  if (id) { priorFocus = document.activeElement as HTMLElement; promptInput.value = state.value.prompt?.input || ''; open('prompt') }
  else if (modal.value === 'prompt') close()
})
function focusDialog() {
  // Preference order: the current section of a settings-style page, then the first control, then Close.
  const root = modalRoot.value
  // A text field is never the starting point: on a controller it would only open the keyboard.
  const target = ['.workflow-sidebar [aria-pressed=true]', '.dialog-body [data-autofocus]', '.dialog-body button:not(:disabled)', '.dialog-body input', 'header button:not(:disabled)']
    .map(selector => root?.querySelector<HTMLElement>(selector)).find(Boolean)
  target?.focus()
}
watch(modal, async () => { await nextTick(); focusDialog() })

// Errors and finished background tasks appear as short-lived toasts instead of a persistent banner.
const toasts = ref<{ id: number; text: string; ok?: boolean }[]>([])
let toastSeq = 0
function friendly(message: string) {
  const text = message.replace(/^Error:\s*/, '')
  return /did not respond to/.test(text) ? t('app.busy') : tn(text)
}
function dismissToast(id: number) { toasts.value = toasts.value.filter(t => t.id !== id) }
function toast(text: string, ok = false) {
  if (toasts.value.some(t => t.text === text)) return
  const id = ++toastSeq
  toasts.value = [...toasts.value.slice(-2), { id, text, ok }]
  if (!ok) play('error')
  setTimeout(() => dismissToast(id), 6000)
}
watch(error, message => {
  if (!message) return
  error.value = ''
  toast(friendly(message))
})
// Steam's keyboard or a link that Gaming Mode could not open: say what to do instead. The first value seen is
// the baseline, so reopening the page never repeats an old notice.
watch(() => state.value.notice?.seq, (seq, before) => {
  const notice = state.value.notice
  if (before === undefined || !notice || !seq) return
  toast(notice.kind === 'keyboard' ? t('app.keyboardUnavailable') : t('app.linkUnavailable', { url: notice.url || '' }))
})
watch(() => state.value.task?.active, (active, wasActive) => {
  const task = state.value.task
  if (!wasActive || active || !task?.title) return
  if (task.status === 'Complete') toast(t('app.taskDone', { title: task.title }), true)
  else if (task.status !== 'Cancelled') toast(t('app.taskFailed', { title: task.title, reason: tn(task.status) }).trim())
})

// Header avatar: the face and hat layers of the selected account's skin.
const avatars = ref<Record<string, string>>({})
const avatar = computed(() => account.value ? avatars.value[account.value.id] : '')
function skinFace(skin: string) {
  return new Promise<string>(resolve => {
    const image = new Image()
    image.onload = () => {
      const canvas = document.createElement('canvas'); canvas.width = canvas.height = 64
      const context = canvas.getContext('2d'); if (!context) return resolve('')
      context.imageSmoothingEnabled = false
      context.drawImage(image, 8, 8, 8, 8, 0, 0, 64, 64)
      context.drawImage(image, 40, 8, 8, 8, 0, 0, 64, 64)
      resolve(canvas.toDataURL())
    }
    image.onerror = () => resolve('')
    image.src = skin
  })
}
watch(() => account.value?.id, async id => {
  if (!id || id in avatars.value) return
  avatars.value[id] = ''
  try {
    const details = await invoke<{ skin?: string }>('accountDetails', { account: id })
    if (details?.skin) avatars.value[id] = await skinFace(details.skin)
  } catch { /* the initial is shown instead */ }
})

// Launch screen: the stages from Play until the game's window has the screen, with timing and honest stall
// messages, then a clear outcome (closed, cancelled, failed to start, crashed).
const overlayRoot = ref<HTMLElement | null>(null)
const launchHidden = ref('')
const launchFailure = ref<{ id: string; status: string; crashed: boolean; logTail: string[] } | null>(null)
const launching = computed(() => state.value.instances.find(i => i.launching))
const wasLaunching = new Set<string>()
// A launch can also fail between two refreshes (no connection fails in a moment), so an outcome counts once
// per launch even when it was never seen running, as long as that launch began after this page opened.
const pageOpened = Date.now(), outcomeShown = new Map<string, number>()
watch(() => state.value.launches, launches => {
  for (const [id, launch] of Object.entries(launches || {})) {
    if (launch.active) { wasLaunching.add(id); continue }
    const unseen = !!launch.startedAt && launch.startedAt >= pageOpened && outcomeShown.get(id) !== launch.startedAt
    if (!wasLaunching.delete(id) && !unseen) continue
    outcomeShown.set(id, launch.startedAt || 0)
    const name = state.value.instances.find(i => i.id === id)?.name || 'Minecraft'
    if (launch.failed) launchFailure.value = { id, status: launch.status, crashed: !!launch.crashed, logTail: launch.logTail || [] }
    else if (launch.stage === 'closed') toast(launch.runningAt && launch.endedAt ? t('launch.closedAfter', { name, time: duration((launch.endedAt - launch.runningAt) / 1000) }) : t('launch.closed', { name }), true)
    else if (launch.stage === 'cancelled') toast(t('launch.cancelled', { name }), true)
  }
})
const stages = ['account', 'files', 'starting'] as const
const now = ref(Date.now())
let ticker: number | undefined
// Stop asks the game to save and close; after 20 s it says so, and Force close (which can lose progress) stays the
// explicit second choice. The card closes itself once the game has gone.
const stopping = ref<{ id: string; at: number } | null>(null)
const stopSlow = computed(() => !!stopping.value && now.value - stopping.value.at > 20000)
async function stopGame(force: boolean) {
  const id = selectedId.value
  if (!id) return
  if (!force) {
    stopping.value = { id, at: Date.now() }
    nextTick(() => modalRoot.value?.querySelector<HTMLElement>('[data-autofocus]')?.focus())
  } else close()
  await action('stop', { id, force })
  if (error.value && stopping.value?.id === id) stopping.value = null
}
watch(() => stopping.value && state.value.instances.find(i => i.id === stopping.value!.id)?.running, stillRunning => {
  if (!stopping.value || stillRunning) return
  stopping.value = null
  if (modal.value === 'stop') close()
})
const stageIndex = computed(() => Math.max(0, stages.indexOf((launchState.value?.stage || 'account') as typeof stages[number])))
const elapsed = computed(() => launchState.value?.startedAt ? Math.max(0, Math.floor((now.value - launchState.value.startedAt) / 1000)) : 0)
// Nothing new for 15 s: say what is still happening instead of looking frozen.
const stalled = computed(() => {
  const s = launchState.value
  if (!s?.stage || !(s.stage in stallKeys)) return ''
  const quiet = (now.value - Math.max(s.changedAt || 0, s.stageAt || 0, s.startedAt || 0)) / 1000
  return quiet >= 15 ? t(stallKeys[s.stage as keyof typeof stallKeys]) : ''
})
const stallKeys = { account: 'launch.stall.account', files: 'launch.stall.files', starting: 'launch.stall.starting' } as const
function clock(seconds: number) { return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}` }
function duration(seconds: number) {
  const hours = Math.floor(seconds / 3600), minutes = Math.round(seconds % 3600 / 60)
  return hours ? t('time.hoursMinutes', { hours, minutes }) : t('time.minutes', { minutes: Math.max(1, minutes) })
}
watch(() => launching.value?.id, id => { if (!id) launchHidden.value = '' })
const overlay = computed(() => launchFailure.value ? 'failed' : launching.value && launchHidden.value !== launching.value.id ? 'launching' : '')
const overlayInstance = computed(() => state.value.instances.find(i => i.id === (launchFailure.value?.id ?? launching.value?.id)))
const launchState = computed(() => launching.value ? state.value.launches?.[launching.value.id] : undefined)
const launchPercent = computed(() => launchState.value?.total ? Math.min(100, Math.round((launchState.value.current || 0) / launchState.value.total * 100)) : 0)
// The launch screen's activity list: each new status line with the time it appeared.
const activity = ref<{ at: number; text: string }[]>([])
watch(() => `${launching.value?.id}:${launchState.value?.startedAt ?? ''}`, () => { activity.value = [] })
watch(() => launchState.value?.status, status => {
  const text = tn(status)
  if (!text || activity.value[activity.value.length - 1]?.text === text) return
  activity.value = [...activity.value.slice(-5), { at: elapsed.value, text }]
})
watch(() => !!overlay.value || !!launching.value || !!stopping.value, on => { window.clearInterval(ticker); if (on) ticker = window.setInterval(() => { now.value = Date.now() }, 1000) }, { immediate: true })
const blocked = computed(() => !!modal.value || !!overlay.value)
watch(overlay, async (value, previous) => {
  if (!value) { gamepad.unregisterContext('prism-launch'); return }
  if (!previous) priorFocus = document.activeElement as HTMLElement
  gamepad.registerContext('prism-launch', {
    root: () => overlayRoot.value,
    onCancel: () => overlay.value === 'failed' ? (launchFailure.value = null) : hideLaunch(),
    onConfirm: confirmFocused,
    onY: launchLog,
    // Cancelling asks first, so a stray press never throws away a launch.
    onX: () => { if (overlay.value === 'launching') open('cancel-launch') },
  })
  await nextTick(); overlayRoot.value?.querySelector<HTMLElement>('button')?.focus()
})
watch(overlay, (value, previous) => { if (!value && previous) nextTick(() => (priorFocus?.isConnected ? priorFocus : (document.querySelector<HTMLElement>('main [data-autofocus]') || document.querySelector<HTMLElement>('main button')))?.focus()) })
function hideLaunch() { if (launching.value) launchHidden.value = launching.value.id }
// Prism's own failure text is technical and English. Known causes get a plain reason; the details stay in Show log.
const NETWORK_FAILURE = /could not download metadata|metadata update task failed|timed out|timeouterror|host\S* not found|hostnotfound|connection refused|network is unreachable|no route to host|name resolution|unknownnetworkerror/i
const failureReason = computed(() => {
  const status = launchFailure.value?.status || ''
  if (launchFailure.value?.crashed) return { kind: 'crashed', text: tn(status) }
  if (NETWORK_FAILURE.test(status)) return { kind: 'network', text: t('launch.reason.network') }
  // Sign-in could not connect, so Prism played offline, but this instance's files were never downloaded.
  if (/required libraries are missing/i.test(status)) return { kind: 'offlineFiles', text: t('launch.reason.offlineFiles') }
  const preLaunch = status.match(/pre-launch command failed with code (-?\d+)/i)
  if (preLaunch) return { kind: 'preLaunch', text: t('launch.reason.preLaunch', { code: preLaunch[1] }) }
  return { kind: 'other', text: tn(status) }
})
function launchLog() { const id = launchFailure.value?.id ?? launching.value?.id; if (id) { selectedId.value = id; open('logs'); refresh() } }
function retryLaunch() { const id = launchFailure.value?.id; launchFailure.value = null; if (id) action('launch', { id }) }

// The Browse tab shows the modpack state as it is now, not as it was when the tab was last open.
watch(tab, value => { if (value === 'Browse') refresh() })
let refreshActive = false, refreshAgain = false
async function refresh() {
  // A change notification during a refresh schedules one more pass instead of being dropped.
  if (refreshActive) { refreshAgain = true; return }
  refreshActive = true
  try {
    do {
      refreshAgain = false
      state.value = await invoke<Snapshot>('snapshot')
      chooseLocale(state.value.language)
      if (state.value.sound) Object.assign(soundSettings, state.value.sound)
      const currentFlow = await invoke<any>('workflow'); flow.value = currentFlow?.id ? currentFlow : null
      if (modal.value === 'logs' && selectedId.value) logLines.value = await invoke('launchLog', { id: selectedId.value })
      // The browser state is fetched only when it changed (its revision rides on the snapshot).
      if ((modal.value === 'modpacks' || tab.value === 'Browse') && (state.value.modpacksRevision !== modpacksRevision || !modpacks.value.hits)) { modpacksRevision = state.value.modpacksRevision; modpacks.value = (await invoke<ModpackState>('modpacks')) || {} }
      if (modal.value === 'resources') modUpdates.value = (await invoke<ModUpdateState>('modUpdates')) || {}
      if (!state.value.instances.some(i => i.id === selectedId.value)) selectedId.value = library.value[0]?.id ?? ''
    } while (refreshAgain)
  } catch (e) { error.value = String(e) } finally { refreshActive = false }
}
// Prism waits on the answer, often while a request of ours (Play's refresh) is still in flight, so a prompt
// answer never waits for busy: a press that was dropped there would look like a frozen launcher.
async function answerPrompt(choice?: string) {
  const id = state.value.prompt?.id
  if (!id) return
  try { await invoke('answerPrompt', { promptId: id, choice, input: promptInput.value }); await refresh() }
  catch (e) { error.value = String(e) }
}
async function action(method: string, args: Record<string, unknown> = {}) {
  if (busy.value) return
  busy.value = true; error.value = ''
  try { await invoke(method, args); await refresh() }
  catch (e) { error.value = String(e) }
  finally { busy.value = false }
}
const gamepad = useGamepad({
  actions: {
    navigateTab(direction) {
      if (blocked.value) return
      const current = Math.max(0, tabs.indexOf(tab.value === 'Accounts' ? 'Settings' : tab.value))
      tab.value = tabs[(current + (direction === 'next' ? 1 : tabs.length - 1)) % tabs.length]
      nextTick(() => (document.querySelector<HTMLElement>('main [data-autofocus]') || document.querySelector<HTMLElement>('main button'))?.focus())
    },
    back: close,
    // Menu: the picked pack's options on the library, all Prism tools elsewhere.
    quickAction: () => afterChordCheck(() => open(tab.value === 'Library' && selected.value ? 'edit' : 'commands')),
    openTasks: () => afterChordCheck(() => open('running')),
  }, onEnablePrompt: () => gamepad.setEnabled(true),
})
const libraryShortcuts = () => blocked.value || tab.value !== 'Library' || !selected.value
useGamepadAction('X', { label: () => selected.value?.running ? t('app.stop_game') : t('app.mods_packs'), handler: () => open(selected.value?.running ? 'stop' : 'resources'), disabled: libraryShortcuts })
useGamepadAction('Y', { label: () => t('app.worlds'), handler: () => open('worlds'), disabled: libraryShortcuts })
let priorFocus: HTMLElement | null = null
// A on a text field starts typing in it (with Steam's keyboard); on anything else it presses it.
function confirmFocused() { const active = document.activeElement; if (isTextField(active)) beginTextEntry(active); else (active as HTMLElement)?.click() }
// View + Menu held for a second switches between the game and Prism (read natively; see GamepadChord).
const chord = ref(0)
let chordSeen = 0
function onChord(fraction: number) { chord.value = fraction; if (fraction > 0) chordSeen = Date.now() }
function afterChordCheck(run: () => void) {
  const pressed = Date.now()
  window.setTimeout(() => { if (chordSeen < pressed - 50 && chord.value === 0) run() }, 300)
}
const running = computed(() => state.value.instances.find(i => i.gameWindow))
let lastKeyboard = 0
function keyboard() {
  if (Date.now() - lastKeyboard < 1000 || !connected.value) return
  lastKeyboard = Date.now()
  invoke('keyboard').catch(e => { error.value = String(e) })
}
// Interface sounds: focus moved by stick, D-pad or keys (not the pointer), buttons pressed, Play.
let lastPointer = 0
function pointerSeen() { lastPointer = Date.now() }
function focusSound(event: FocusEvent) {
  if (Date.now() - lastPointer > 400 && event.relatedTarget && (event.target as Element).matches?.('button, input, select, textarea')) play('navigate')
}
function clickSound(event: MouseEvent) {
  const button = (event.target as Element).closest?.('button')
  if (!button || button.disabled || button.closest('[data-sound=none]')) return
  const kind = button.dataset.action
  if (kind === 'close-dialog') return  // close() plays its own sound
  play(kind === 'play' ? 'launch' : 'confirm')
}
function close() {
  // Outside a page, B steps back to the library tab (Browse first closes its own pickers).
  if (!modal.value && !overlay.value && tab.value !== 'Library') {
    if (!window.dispatchEvent(new Event('prism-back', { cancelable: true }))) return
    play('back')
    tab.value = 'Library'
    nextTick(() => (document.querySelector<HTMLElement>('main [data-autofocus]') || document.querySelector<HTMLElement>('main button'))?.focus())
    return
  }
  // A page with its own inner level (a chosen pack, an open screenshot) steps back there first.
  if (modal.value && modal.value !== 'setup') play('back')
  if (modal.value && !window.dispatchEvent(new Event('prism-back', { cancelable: true }))) return
  if (modal.value === 'workflow') { nativePanel.value?.close(); return }
  if (modal.value === 'setup' && state.value.setup?.required) return
  if (modal.value === 'prompt' && state.value.prompt?.id) {
    answerPrompt(state.value.prompt.choices?.[0]); return
  }
  modal.value = ''; gamepad.unregisterContext('prism-dialog')
  nextTick(() => priorFocus?.focus())
}
async function open(which: string) {
  // Modpacks live on the Browse tab now; anything that asks for them goes there.
  if (which === 'modpacks') {
    if (modal.value) { modal.value = ''; gamepad.unregisterContext('prism-dialog') }
    tab.value = 'Browse'
    return
  }
  if (state.value.setup?.required && !['setup', 'prompt'].includes(which)) return
  if (!['running', 'create', 'import', 'modpacks', 'preferences', 'launcher', 'cancel-launch', 'prompt', 'setup', 'accounts', 'workflow', 'commands'].includes(which) && !selected.value) return
  priorFocus = document.activeElement as HTMLElement
  if (which === 'edit' && selected.value) {
    name.value = selected.value.name; minMemory.value = selected.value.minMemory; maxMemory.value = selected.value.maxMemory
  }
  modal.value = which
  gamepad.registerContext('prism-dialog', {
    root: () => modalRoot.value, onCancel: close,
    onConfirm: confirmFocused,
  })
  await nextTick(); focusDialog()
  if (which === 'resources') await loadResources()
  if (which === 'create') await action('loadVersions')
  if (which === 'modpacks') await refresh()
  if (which === 'commands') commands.value = await invoke('commands', { id: selectedId.value })
}
async function loadResources() {
  if (!selected.value) return
  try { resources.value = await invoke<Resource[]>('resources', { id: selectedId.value, kind: kind.value }) }
  catch (e) { error.value = String(e) }
}
async function toggle(resource: Resource) {
  await action('setResourceEnabled', { id: selectedId.value, kind: kind.value, resourceId: resource.id, enabled: !resource.enabled })
  await loadResources()
}
function key(event: KeyboardEvent) {
  if (event.key === 'Escape') { if (!modal.value && overlay.value) { overlay.value === 'failed' ? (launchFailure.value = null) : hideLaunch(); return } close(); return }
  const scope = modal.value ? modalRoot.value : overlay.value ? overlayRoot.value : null
  if (event.key === 'Tab' && scope) {
    const nodes = [...scope.querySelectorAll<HTMLElement>('button:not(:disabled),input,select,textarea')]
    const index = nodes.indexOf(document.activeElement as HTMLElement)
    event.preventDefault(); nodes[(index + (event.shiftKey ? nodes.length - 1 : 1)) % nodes.length]?.focus(); return
  }
  const input = event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement || event.target instanceof HTMLSelectElement
  if (input) return
  const directions: Record<string, 'up' | 'down' | 'left' | 'right'> = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' }
  if (directions[event.key]) { event.preventDefault(); moveFocus(directions[event.key], scope) }
}
onMounted(async () => {
  window.addEventListener('keydown', key)
  window.addEventListener('prism-open-keyboard', keyboard)
  window.addEventListener('focusin', focusSound)
  window.addEventListener('pointerdown', pointerSeen, true)
  window.addEventListener('click', clickSound, true)
  try { await connect(refresh, onChord); connected.value = true; await refresh(); await nextTick(); (document.querySelector<HTMLElement>('main [data-autofocus]') || document.querySelector<HTMLElement>('main button'))?.focus() }
  catch (e) { error.value = String(e) }
})
onUnmounted(() => { window.removeEventListener('keydown', key); window.removeEventListener('prism-open-keyboard', keyboard); window.removeEventListener('focusin', focusSound); window.removeEventListener('pointerdown', pointerSeen, true); window.removeEventListener('click', clickSound, true); gamepad.unregisterContext('prism-dialog'); gamepad.unregisterContext('prism-launch') })
</script>

<template>
  <div class="bp-container" :data-connected="connected">
    <div class="bp-ambient-glow" />
    <header class="bp-header" :inert="blocked">
      <div class="bp-brand"><strong>Prism Console</strong></div>
      <nav class="bp-nav" :aria-label="t('app.main_navigation')">
        <ButtonGlyph button="LB" class="nav-glyph" />
        <button v-for="item in tabs" :key="item" class="bp-nav-btn" :class="{ active: tab === item }" :data-action="item === 'Browse' ? 'browse-modpacks' : undefined" :aria-current="tab === item ? 'page' : undefined" @click="tab = item">{{ t(`nav.${item}` as MessageKey) }}</button>
        <ButtonGlyph button="RB" class="nav-glyph" />
      </nav>
      <div class="header-end">
        <button class="account-chip" @click="tab = 'Accounts'">
          <img v-if="avatar" :src="avatar" alt="" class="avatar"><span v-else class="avatar avatar-initial" aria-hidden="true">{{ (account?.name || '?').charAt(0).toUpperCase() }}</span>
          <span>{{ account?.name || t('app.signInButton') }}</span>
        </button>
      </div>
    </header>
    <main :inert="blocked" :aria-busy="busy">
      <section v-if="running" class="running-banner" role="status">
        <img :src="running.icon || fallbackIcon" alt="">
        <strong>{{ t('running.banner', { name: running.name }) }}</strong>
        <span class="hint"><ButtonGlyph button="View" /><ButtonGlyph button="Menu" /> {{ t('running.hint') }}</span>
        <button class="primary" data-action="back-to-game" @click="action('backToGame')">{{ t('running.backToGame') }}</button>
      </section>
      <template v-if="tab === 'Library'">
        <section class="bp-hero lib-hero" v-if="selected">
          <div class="hero-info">
            <h1>{{ selected.name }}</h1>
            <p v-if="playedLine" class="hero-played">{{ playedLine }}</p>
            <p class="hero-meta">{{ metaLine }}</p>
            <!-- What A, X and Y do for this pack. Tappable, but the stick moves along the shelf, never up here. -->
            <div class="hero-actions" data-gamepad-skip>
              <button tabindex="-1" class="hero-action primary-action" data-action="play" :disabled="busy || !connected || (!selected.running && (!selected.canLaunch || selected.launching))" @click="playSelected">
                <ButtonGlyph button="A" accent large />{{ selected.running ? t('running.backToGame') : selected.launching ? t('app.starting') : t('app.play') }}
              </button>
              <button v-if="selected.running" class="hero-action" data-action="stop-game" :disabled="busy" @click="open('stop')"><ButtonGlyph button="X" />{{ stopping?.id === selected.id ? t('app.closingGame') : t('app.stop_game') }}</button>
              <button v-else class="hero-action" data-action="mods" @click="open('resources')"><ButtonGlyph button="X" />{{ t('app.mods_packs') }}</button>
              <button tabindex="-1" class="hero-action" data-action="worlds" @click="open('worlds')"><ButtonGlyph button="Y" />{{ t('app.worlds') }}</button>
            </div>
          </div>
          <PackArt :src="selected.icon" :name="selected.name" :size="288" />
        </section>
        <section v-else class="bp-hero lib-hero empty">
          <div class="hero-info"><h1>{{ connected ? t('app.emptyLibrary') : t('app.connecting') }}</h1><p v-if="connected" class="hero-played">{{ t('app.create_or_import_your_first') }}</p></div>
        </section>
        <section class="shelf" :aria-label="t('app.instances')">
          <h2>{{ t('app.instances') }} <span>{{ state.instances.length }}</span></h2>
          <div class="shelf-row">
            <button v-for="instance in library" :key="instance.id" class="instance-card" :class="{ selected: instance.id === selectedId }" :data-id="instance.id" :data-autofocus="instance.id === selectedId || undefined" @focus="focusTile(instance.id)" @click="tileClick(instance.id)">
              <PackArt :src="instance.icon" :name="instance.name" :size="192" />
              <strong>{{ instance.name }}</strong><span>{{ instance.running ? t('app.running') : [instance.loader, instance.version].filter(Boolean).join(' · ') }}</span>
            </button>
            <button v-if="!library.length" class="instance-card add-tile" data-action="empty-browse" :data-autofocus="true" @click="tab = 'Browse'">
              <span class="pack-art add-art" aria-hidden="true">↓</span><strong>{{ t('app.browse_modpacks') }}</strong><span>{{ t('app.browseTileHint') }}</span>
            </button>
            <button class="instance-card add-tile" data-action="add-instance" :disabled="!connected" @click="open('create')">
              <span class="pack-art add-art" aria-hidden="true">+</span><strong>{{ t('app.addInstanceTile') }}</strong><span>{{ t('app.addInstanceHint') }}</span>
            </button>
          </div>
        </section>
      </template>
      <ModpackBrowser v-else-if="tab === 'Browse'" :state="modpacks" :busy="busy || !!state.task?.active" @installed="tab = 'Library'; refresh()" @error="error = $event" />
      <section v-else-if="tab === 'Accounts'" class="page"><h1>{{ t('app.accounts') }}</h1><p class="muted">{{ t('app.choose_the_account_prism_uses') }}</p>
        <button v-for="a in state.accounts" :key="a.id" class="row" :aria-pressed="a.selected" :disabled="busy" @click="action('selectAccount', { id: a.id })">{{ a.name }} <span>{{ a.selected ? t('app.inUse') : t('app.useAccount') }}</span></button>
        <div class="login-state" v-if="state.login?.status" role="status"><p>{{ state.login.status }}</p>
          <template v-if="state.login.active && state.login.code"><p>{{ t('app.signInSteps') }}</p><p class="device-url">{{ state.login.url }}</p><p class="device-code">{{ state.login.code }}</p><p>{{ t('app.complete_sign_in_with_microsoft') }}</p></template>
        </div>
        <button v-if="state.login?.active" @click="action('cancelLogin')">{{ t('app.cancel_sign_in') }}</button>
        <button v-else :disabled="!connected || busy" @click="action('beginLogin')">{{ t('app.sign_in_with_microsoft') }}</button>
        <button v-for="a in state.accounts" :key="'manage-' + a.id" :disabled="busy" @click="managedAccount = a.id; open('accounts')">{{ t('app.manageAccount', { name: a.name }) }}</button>
      </section>
      <section v-else class="page"><h1>{{ t('app.settings') }}</h1><p class="muted">{{ t('app.prism_manages_your_java_installations') }}</p>
        <button @click="open('preferences')" :disabled="!connected">{{ t('app.java_memory_performance') }}</button>
        <button data-action="launcher-settings" @click="open('launcher')" :disabled="!connected">{{ t('app.launcher_settings') }}</button>
        <button @click="action('nativeDialog', { route: 'settings' })" :disabled="!connected">{{ t('app.advanced_prism_settings') }}</button>
        <button @click="open('commands')">{{ t('app.all_prism_tools') }}</button>
        <h3 class="section-title">{{ t('app.language') }}</h3>
        <div class="control-segmented" role="radiogroup" :aria-label="t('app.language')">
          <button role="radio" data-action="language-en" :aria-checked="locale === 'en'" :disabled="busy" @click="action('setLanguage', { language: 'en_US' })">English</button>
          <button role="radio" data-action="language-pt-BR" :aria-checked="locale === 'pt-BR'" :disabled="busy" @click="action('setLanguage', { language: 'pt_BR' })">Português (Brasil)</button>
        </div>
        <h3 class="section-title">{{ t('controls.title') }}</h3>
        <dl class="controls-list">
          <div><dt><ButtonGlyph button="A" /></dt><dd>{{ t('controls.select') }}</dd></div>
          <div><dt><ButtonGlyph button="B" /></dt><dd>{{ t('controls.back') }}</dd></div>
          <div><dt><ButtonGlyph button="X" /></dt><dd>{{ t('controls.mods') }}</dd></div>
          <div><dt><ButtonGlyph button="Y" /></dt><dd>{{ t('controls.worlds') }}</dd></div>
          <div><dt><ButtonGlyph button="LB" /><ButtonGlyph button="RB" /></dt><dd>{{ t('controls.tabs') }}</dd></div>
          <div><dt><ButtonGlyph button="Menu" /></dt><dd>{{ t('controls.menu') }}</dd></div>
          <div><dt><ButtonGlyph button="View" /></dt><dd>{{ t('controls.view') }}</dd></div>
          <div><dt><ButtonGlyph button="View" /><ButtonGlyph button="Menu" /></dt><dd>{{ t('controls.chord') }}</dd></div>
          <div><dt></dt><dd>{{ t('controls.scroll') }}</dd></div>
        </dl>
        <p class="muted">{{ t('app.controllerName', { name: gamepad.name.value || t('app.noController') }) }}</p>
      </section>
    </main>
    <footer :inert="blocked">
      <span class="footer-status">
        <button v-if="launching && launchHidden === launching.id" class="status-chip" data-action="launch-show" @click="launchHidden = ''">{{ t('launch.chip', { name: launching.name, stage: t(`launch.stage.${stages[stageIndex]}`) }) }}<template v-if="launchPercent"> · {{ launchPercent }}%</template></button>
        <button v-if="state.task?.title" class="status-chip" @click="open('running')">{{ state.task.active ? t('app.working') : t('app.tasks') }}</button>
        <span v-if="!connected" class="disconnected">{{ t('app.not_connected_to_prism') }}</span>
      </span>
      <template v-if="tab === 'Library'"><span v-if="selected" class="hint"><ButtonGlyph button="Menu" /> {{ t('app.instanceOptions') }}</span></template>
      <template v-else-if="tab === 'Browse'"><span class="hint"><ButtonGlyph button="Y" /> {{ t('modpacks.search') }}</span><span class="hint"><span class="glyph">L2</span> {{ t('modpacks.filter.title') }}</span><span class="hint"><ButtonGlyph button="B" /> {{ t('nav.Library') }}</span></template>
      <template v-else><span class="hint"><ButtonGlyph button="A" /> {{ t('app.select') }}</span><span class="hint"><ButtonGlyph button="B" /> {{ t('nav.Library') }}</span></template>
    </footer>

    <div v-if="overlay" ref="overlayRoot" class="launch-overlay" :class="{ failed: overlay === 'failed' }" role="dialog" aria-modal="true" aria-labelledby="launch-title" :inert="!!modal">
      <div class="launch-top"><strong class="wide">Prism Console</strong></div>
      <section class="lib-hero launch-hero">
        <div class="hero-info">
          <h1 id="launch-title">{{ overlayInstance?.name || 'Minecraft' }}</h1>
          <template v-if="overlay === 'launching'">
            <p class="launch-step"><span aria-current="step">{{ t(`launch.stage.${stages[stageIndex]}`) }}</span><span class="dim"> · {{ t('launch.stepOf', { step: stageIndex + 1, total: stages.length }) }}</span></p>
            <div class="launch-progress" :class="{ indeterminate: !launchPercent }"><span :style="{ width: launchPercent + '%' }" /></div>
            <div class="launch-meta"><span role="status" class="launch-status">{{ tn(launchState?.status) || t('launch.preparing') }}</span><span class="launch-timing">{{ clock(elapsed) }}<template v-if="overlayInstance?.launchSeconds"> · {{ t('launch.usually', { seconds: overlayInstance.launchSeconds }) }}</template></span></div>
            <p v-if="stalled" class="launch-stall" role="status">{{ stalled }}</p>
          </template>
          <template v-else>
            <p class="launch-step failed-step">{{ launchFailure?.crashed ? t('launch.crashedEyebrow') : t('launch.failedEyebrow') }}</p>
            <p role="alert" class="launch-status" :data-reason="failureReason.kind">{{ failureReason.text }}</p>
          </template>
        </div>
        <PackArt :src="overlayInstance?.icon" :name="overlayInstance?.name || 'Minecraft'" :size="288" />
      </section>
      <section class="launch-activity">
        <template v-if="overlay === 'launching'">
          <h2>{{ t('launch.activity') }}</h2>
          <ol><li v-for="(item, i) in activity" :key="i" :class="{ current: i === activity.length - 1 }"><span class="time">{{ clock(item.at) }}</span><span>{{ item.text }}</span></li></ol>
        </template>
        <pre v-else-if="launchFailure?.crashed && launchFailure.logTail.length" class="launch-tail" tabindex="0" :aria-label="t('launch.lastLogLines')">{{ launchFailure.logTail.join('\n') }}</pre>
      </section>
      <footer class="launch-footer">
        <span v-if="overlay === 'launching'" class="launch-hint">{{ t('launch.chordHint') }}</span>
        <template v-if="overlay === 'launching'">
          <button class="hint" data-action="launch-hide" @click="hideLaunch"><ButtonGlyph button="B" />{{ t('nav.Library') }}</button>
          <button class="hint" data-action="launch-log" @click="launchLog"><ButtonGlyph button="Y" />{{ t('launch.log') }}</button>
          <button class="hint" data-action="launch-cancel" @click="open('cancel-launch')"><ButtonGlyph button="X" />{{ t('launch.cancelShort') }}</button>
        </template>
        <template v-else>
          <button class="hint" data-action="launch-retry" :disabled="busy" @click="retryLaunch"><ButtonGlyph button="A" accent />{{ t('launch.tryAgain') }}</button>
          <button class="hint" data-action="launch-log" @click="launchLog"><ButtonGlyph button="Y" />{{ t('launch.log') }}</button>
          <button class="hint" data-action="launch-close" @click="launchFailure = null"><ButtonGlyph button="B" />{{ t('common.close') }}</button>
        </template>
      </footer>
    </div>

    <div v-if="modal" class="modal-scrim" :class="{ compact }"><section ref="modalRoot" role="dialog" aria-modal="true" aria-labelledby="dialog-title" class="dialog" :class="{ 'dialog-compact': compact }">
      <header><h2 id="dialog-title">{{ dialogTitle }}</h2><button data-action="close-dialog" :disabled="modal === 'setup'" @click="close" :aria-label="t('app.close_dialog')">×</button></header>
      <div class="dialog-body">
        <template v-if="modal === 'setup'"><p>{{ t('app.your_prism_instances_and_accounts') }}</p><p>{{ t('app.choose_automatic_java_downloads_or') }}</p><div class="actions"><button :disabled="busy" @click="action('completeSetup', { automaticJava: true })">{{ t('app.use_automatic_java') }}</button><button :disabled="busy" @click="action('completeSetup', { automaticJava: !!state.setup?.automaticJava, keepExisting: true })">{{ t('app.keep_current_settings') }}</button></div></template>
        <NativeWorkflow v-else-if="modal === 'workflow' && flow" ref="nativePanel" :flow="flow" @refresh="refresh" />
        <template v-else-if="modal === 'commands'"><input v-model="commandFilter" :placeholder="t('app.search_tools')" :aria-label="t('app.search_prism_tools')"><button class="row" v-for="c in commands.filter(c => c.text.toLowerCase().includes(commandFilter.toLowerCase()))" :key="c.id" :disabled="!c.enabled" @click="action('workflowAction', { control: c.id, op: 'click' })">{{ c.text }}</button></template>
        <LauncherSettings v-else-if="modal === 'launcher'" @error="error = $event" />
        <AccountPanel v-else-if="modal === 'accounts'" :account="managedAccount" @changed="refresh" @removed="close(); refresh()" />
        <template v-else-if="modal === 'prompt'"><p>{{ tn(state.prompt?.text) }}</p><label v-if="state.prompt?.textInput">{{ t('app.player_name') }}<input v-model="promptInput" maxlength="16"></label><div class="actions"><button v-for="choice in state.prompt?.choices" :key="choice" :data-choice="choice" @click="answerPrompt(choice)">{{ tn(choice) }}</button></div></template>
        <WorldsPage v-else-if="modal === 'worlds'" :instance-id="selectedId" :running="!!selected?.running" :busy="busy || !!state.task?.active" @play="close(); action('launch', { id: selectedId, ...$event })" @error="error = $event" />
        <Workflows v-else-if="['import', 'preferences', 'advanced', 'versions', 'notes', 'export', 'copy'].includes(modal)" :mode="modal" :instance-id="selectedId" @changed="refresh" />
        <Catalog v-else-if="modal === 'catalog'" :instance-id="selectedId" :kind="kind" />
        <template v-else-if="modal === 'logs'">
          <p role="status">{{ tn(state.launches?.[selectedId]?.status) }}</p>
          <button v-if="selected?.launching" @click="action('cancelLaunch', { id: selectedId })">{{ t('launch.cancel') }}</button>
          <pre class="launch-log" tabindex="0" :aria-label="t('app.game_log')">{{ logLines.join('\n') || t('app.noLaunchOutput') }}</pre>
        </template>
        <template v-else-if="modal === 'create'">
          <label>{{ t('app.name') }}<input v-model="newName" maxlength="120"></label><h3>Minecraft version {{ newVersion }}</h3>
          <div class="version-list" :aria-label="t('app.minecraft_versions')"><button v-for="v in state.versions" :key="v" :aria-pressed="newVersion === v" @click="newVersion = v">{{ v }}</button></div>
          <p v-if="state.task?.title" role="status">{{ state.task.title }}: {{ state.task.status }}</p>
          <div class="actions"><button class="primary" :disabled="busy || state.task?.active || !newName.trim() || !newVersion" @click="action('createInstance', { name: newName, version: newVersion })">{{ t('app.create_vanilla_instance') }}</button>
          <button @click="open('modpacks')">{{ t('app.browse_modrinth_modpacks') }}</button><button @click="modal = 'import'">{{ t('app.import_modpack_file') }}</button><button @click="action('nativeDialog', { route: 'add' })">{{ t('app.other_modpack_providers') }}</button></div>
        </template>
        <template v-else-if="modal === 'resources'">
          <div class="actions" role="tablist"><button v-for="k in ['mods', 'resourcepacks', 'shaderpacks']" :key="k" :aria-pressed="kind === k" @click="kind = k; loadResources()">{{ t(`app.kind.${k}` as MessageKey) }}</button><button @click="loadResources">{{ t('app.refresh') }}</button></div>
          <p class="muted">{{ t('app.resource_details_load_in_the') }}</p>
          <div class="resource-list"><button class="row setting-toggle" role="switch" v-for="r in resources" :key="r.id" :disabled="busy || selected?.running" :aria-checked="r.enabled" @click="toggle(r)"><span class="setting-text"><span>{{ r.name }}</span><small v-if="r.version">{{ r.version }}</small></span><span class="switch" aria-hidden="true"><span /></span></button></div>
          <ModUpdates v-if="kind === 'mods'" :instance-id="selectedId" :state="modUpdates" :busy="busy || !!state.task?.active" :running="!!selected?.running" @error="error = $event" />
          <div class="actions"><button class="primary" @click="modal = 'catalog'">{{ t('app.download_from_modrinth') }}</button>
          <button @click="resourceBrowser = !resourceBrowser">{{ t('app.browse_local_archive') }}</button>
          <button @click="action('instanceDialog', { id: selectedId, page: kind })">{{ t('app.other_providers') }}</button></div>
          <FilePicker v-if="resourceBrowser" kind="resource" @select="localResource = $event; resourceBrowser = false" />
          <template v-if="localResource"><p>{{ localResource }}</p><button :disabled="busy || selected?.running" @click="action('installLocalResource', { id: selectedId, kind, path: localResource }); loadResources()">{{ t('app.install_selected_archive') }}</button></template>
        </template>
        <template v-else-if="modal === 'edit'">
          <label>{{ t('app.instance_name') }}<input v-model="name" maxlength="120"></label>
          <label>{{ t('app.minimum_memory_mib') }}<input v-model.number="minMemory" type="number" min="256" max="65536" step="256"></label>
          <label>{{ t('app.maximum_memory_mib') }}<input v-model.number="maxMemory" type="number" min="256" max="65536" step="256"></label>
          <div class="actions"><button class="primary" :disabled="busy || selected?.running" @click="action('saveInstance', { id: selectedId, name, minMemory, maxMemory })">{{ t('app.save') }}</button><button @click="modal = 'versions'">{{ t('app.versions_loaders') }}</button><button @click="modal = 'advanced'">{{ t('app.advanced') }}</button></div>
          <h3 class="section-title">{{ t('app.more') }}</h3>
          <div class="actions"><button data-action="add-to-steam" :disabled="busy || state.task?.active" @click="action('addToSteam', { id: selectedId })">{{ selected?.steamAppId ? t('app.updateSteamTile') : t('app.addToSteam') }}</button><button @click="modal = 'copy'">{{ t('app.copy_instance') }}</button><button @click="modal = 'worlds'">{{ t('app.worlds') }}</button><button @click="modal = 'notes'">{{ t('app.notes') }}</button><button @click="modal = 'export'">{{ t('app.export_instance') }}</button><button @click="action('instanceDialog', { id: selectedId, page: 'version' })">{{ t('app.full_instance_editor') }}</button><button @click="open('commands')">{{ t('app.all_instance_tools') }}</button></div>
        </template>
        <template v-else-if="modal === 'cancel-launch'"><p>{{ t('launch.cancelQuestion', { name: launching?.name || 'Minecraft' }) }}</p><div class="actions"><button @click="close">{{ t('launch.keepStarting') }}</button><button data-action="confirm-cancel-launch" :disabled="busy || !launching" @click="action('cancelLaunch', { id: launching!.id }); close()">{{ t('launch.cancelShort') }}</button></div></template>
        <template v-else-if="modal === 'stop' && stopping?.id === selectedId"><p role="status" class="stop-status">{{ stopSlow ? t('app.stopSlow') : t('app.savingAndClosing') }}</p><p class="muted">{{ t('app.forceCloseWarning') }}</p><div class="actions"><button data-autofocus @click="close">{{ t('app.keepWaiting') }}</button><button data-action="force-close" :disabled="busy" @click="stopGame(true)">{{ t('app.forceClose') }}</button></div></template>
        <template v-else-if="modal === 'stop'"><p>{{ t('app.stopSaves') }}</p><div class="actions"><button @click="close">{{ t('app.keep_playing') }}</button><button data-action="confirm-stop" :disabled="busy" @click="stopGame(false)">{{ t('app.stop_game') }}</button></div></template>
        <template v-else><section v-if="state.task?.title"><h3>{{ state.task.title }}</h3><p role="status">{{ tn(state.task.status) }}</p><button v-if="state.task.active" @click="action('cancelTask')">{{ t('app.cancel_task') }}</button><progress v-if="state.task.active" :value="state.task.total ? state.task.current : undefined" :max="state.task.total || 1" /></section><p v-if="!state.instances.some(i => i.running)">{{ t('app.no_games_are_running') }}</p><div class="row" v-for="i in state.instances.filter(i => i.running)" :key="i.id">{{ i.name }}</div></template>
      </div>
      <div class="dialog-hints" aria-hidden="true"><span class="hint"><ButtonGlyph button="A" /> {{ t('app.select') }}</span><span v-if="modal !== 'setup'" class="hint"><ButtonGlyph button="B" /> {{ t('app.back') }}</span></div>
    </section></div>

    <div v-if="chord > 0" class="chord-ring" role="status" :style="{ '--progress': chord }">
      <span class="chord-dial" aria-hidden="true"><ButtonGlyph button="View" /><ButtonGlyph button="Menu" /></span>
      <span>{{ running ? t('chord.toGame') : t('chord.toLauncher') }}</span>
    </div>

    <div class="toasts" role="status" aria-live="polite">
      <div v-for="item in toasts" :key="item.id" class="toast" :class="{ 'toast-ok': item.ok }" :role="item.ok ? undefined : 'alert'"><span>{{ item.text }}</span><button :aria-label="t('app.dismiss')" @click="dismissToast(item.id)">×</button></div>
    </div>
  </div>
</template>
