export interface Instance {
  id: string; name: string; group: string; icon: string; version: string;
  /** Mod loader and its version, e.g. "NeoForge 21.1.252"; empty for vanilla. */ loader?: string
  running: boolean; launching: boolean; canLaunch: boolean; notes: string; minMemory: number; maxMemory: number
  /** Epoch milliseconds of the last launch, 0 if never. */ lastLaunch?: number
  /** Total seconds played. */ playTime?: number
  /** Steam tile created by "Add to Steam", 0 if none. */ steamAppId?: number
  /** Seconds from Play to the game window on the last launch, 0 if unknown. */ launchSeconds?: number
  /** The game is running and its window has had the screen. */ gameWindow?: boolean
}
export interface Account { id: string; name: string; selected: boolean }
export interface Resource { id: string; name: string; version: string; enabled: boolean }
export interface ModpackHit { id: string; slug: string; title: string; description: string; author: string; downloads: number; categories?: string[]; minecraft: string; hasIcon?: boolean; /** ISO date of the last update. */ updated?: string }
export interface ModpackRelease { id: string; name: string; number: string; type: string; minecraft: string[]; loaders: string[]; published: string; size: number }
export interface ModpackState {
  query?: string; loading?: boolean; error?: string; total?: number; hits?: ModpackHit[]
  project?: string; versions?: ModpackRelease[]; versionsLoading?: boolean
  /** The filters of the last search, and Minecraft releases (newest first) for the version filter. */
  filters?: { loader: string; gameVersion: string; sort: string }; gameVersions?: string[]
}
export interface ModUpdate { name: string; from: string; to: string; beta: boolean; changelog: string }
export interface ModUpdateState {
  instance?: string; phase?: 'checking' | 'ready' | 'failed'; managed?: boolean; total?: number; checked?: number; updates?: ModUpdate[]; error?: string
  /** What the scan found: mods pinned by a modpack update with the pack, not one by one. */
  scan?: { files: number; pinned: number; unidentified: number; modrinth: number }
}
/** One launch, from Play to exit. Times are epoch milliseconds. */
export interface LaunchState {
  active: boolean; status: string; failed?: boolean; crashed?: boolean; logTail?: string[]; current?: number; total?: number
  stage?: 'account' | 'files' | 'starting' | 'running' | 'closed' | 'cancelled'
  startedAt?: number; stageAt?: number; changedAt?: number; runningAt?: number; endedAt?: number
}
export interface Snapshot {
  setup?: { required: boolean; automaticJava: boolean };
  prompt?: { id?: string; title?: string; text?: string; choices?: string[]; input?: string; textInput?: boolean };
  launches?: Record<string, LaunchState>;
  // Something Prism could not do for the renderer, said once per seq: Steam's keyboard or a link that did not open.
  notice?: { seq: number; kind: 'keyboard' | 'link'; url?: string };
  instances: Instance[]; accounts: Account[]; versions?: string[];
  task?: { active: boolean; title?: string; status?: string; current?: number; total?: number };
  login?: { active: boolean; status?: string; code?: string; url?: string; expiresIn?: number }
  /** Prism's Language setting, e.g. "pt_BR". */ language?: string
  /** Moves whenever the modpack browser state changes. */ modpacksRevision?: number
  /** Interface sounds (Launcher settings). */ sound?: { enabled: boolean; volume: number }
}
export interface Reply<T = unknown> { ok: boolean; error?: string; data?: T }
interface NativeBridge {
  request(method: string, args: Record<string, unknown>, callback: (reply: Reply) => void): void
  changed: { connect(callback: () => void): void }
  chordProgress?: { connect(callback: (fraction: number) => void): void }
}
declare global {
  interface Window {
    qt?: { webChannelTransport: unknown }
    QWebChannel?: new (transport: unknown, callback: (channel: { objects: { prism: NativeBridge } }) => void) => unknown
  }
}
let bridge: NativeBridge | undefined
export async function connect(onChange: () => void, onChord?: (fraction: number) => void): Promise<void> {
  if (!window.qt || !window.QWebChannel) throw new Error('Open this interface through the Prism Console build. No launcher is connected.')
  await new Promise<void>((resolve) => {
    new window.QWebChannel!(window.qt!.webChannelTransport, channel => {
      bridge = channel.objects.prism
      bridge.changed.connect(onChange)
      if (onChord) bridge.chordProgress?.connect(onChord)
      resolve()
    })
  })
}
export function invoke<T>(method: string, args: Record<string, unknown> = {}): Promise<T> {
  return new Promise((resolve, reject) => {
    if (!bridge) return reject(new Error('Prism is not connected'))
    const timeout = setTimeout(() => reject(new Error(`Prism did not respond to ${method}`)), 30000)
    bridge.request(method, args, reply => {
      clearTimeout(timeout)
      if (reply.ok) resolve(reply.data as T)
      else reject(new Error(reply.error || 'Operation failed'))
    })
  })
}
