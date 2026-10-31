// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { ref } from 'vue'
import App from './App.vue'
const mocks = vi.hoisted(() => ({ invoke: vi.fn(), connect: vi.fn() }))
vi.mock('./bridge', () => mocks)
vi.mock('./upstream/gamepad', () => ({ moveFocus: vi.fn(), useGamepadAction: vi.fn(), useGamepad: () => ({ connected: ref(false), type: ref('xbox'), isPlayStation: ref(false), labels: ref({ confirm: 'A', cancel: 'B', backspace: 'X', keyboard: 'Y', bumpers: 'LB/RB' }),
  setEnabled() {}, registerContext() {}, unregisterContext() {}, name: ref(''), buttonA: ref('A'), buttonB: ref('B'),
}) }))
const instance = { id: 'stable-id', name: 'CrazyCraft', version: '1.21.1', running: false, launching: false, canLaunch: true, icon: '', group: '', minMemory: 2048, maxMemory: 10240 }
describe('Prism controller UI', () => {
  let wrapper: ReturnType<typeof mount> | undefined
  afterEach(() => { wrapper?.unmount(); vi.resetAllMocks() })
  const button = (text: string) => wrapper!.findAll('button').find(b => b.text() === text)!
  async function setup(value = instance) {
    mocks.connect.mockResolvedValue(undefined)
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [value], accounts: [] } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
  }
  it('uses a stable Prism instance ID and sends one launch per activation', async () => {
    await setup()
    expect(wrapper!.text()).not.toContain('Create or import your first instance.')
    await button('▶ Play').trigger('click'); await flushPromises()
    expect(mocks.invoke.mock.calls.filter(c => c[0] === 'launch')).toEqual([['launch', { id: 'stable-id' }]])
  })
  it('does not offer another launch while Prism is preparing the same instance', async () => {
    await setup({ ...instance, launching: true, canLaunch: false })
    expect(button('Starting…').attributes('disabled')).toBeDefined()
  })
  it('requires confirmation before stopping a game', async () => {
    await setup({ ...instance, running: true, canLaunch: false })
    await button('Stop game').trigger('click'); await flushPromises()
    expect(mocks.invoke.mock.calls.some(c => c[0] === 'stop')).toBe(false)
    await button('Keep playing').trigger('click')
    expect(wrapper!.find('[role=dialog]').exists()).toBe(false)
  })
  it('keeps onboarding open until settings are explicitly chosen', async () => {
    let required = true
    mocks.connect.mockResolvedValue(undefined)
    mocks.invoke.mockImplementation(async (method: string) => {
      if (method === 'completeSetup') required = false
      if (method === 'snapshot') return { instances: [], accounts: [], setup: { required, automaticJava: false } }
    })
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    expect(wrapper.find('[role=dialog]').text()).toContain('Welcome to Prism Console')
    expect(wrapper.find('[aria-label="Close dialog"]').attributes('disabled')).toBeDefined()
    await button('Keep current settings').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('completeSetup', { automaticJava: false, keepExisting: true })
    expect(wrapper.find('[role=dialog]').exists()).toBe(false)
  })

  it('shows launch progress over the library and a retryable failure screen', async () => {
    let launch: any = { active: true, status: 'Downloading assets', current: 5, total: 10 }
    let notify = () => {}
    mocks.connect.mockImplementation(async (callback: () => void) => { notify = callback })
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot'
      ? { instances: [{ ...instance, launching: launch.active, canLaunch: !launch.active }], accounts: [], launches: { 'stable-id': launch } } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    expect(wrapper.find('.launch-overlay').text()).toContain('Downloading assets')
    expect(wrapper.find('.launch-progress span').attributes('style')).toContain('width: 50%')
    launch = { active: false, failed: true, status: 'Minecraft account refresh timed out' }
    notify(); await flushPromises()
    expect(wrapper.find('.launch-overlay [role=alert]').text()).toBe("Couldn't reach Minecraft's servers. Check your connection, then try again.")
    await button('Try again').trigger('click'); await flushPromises()
    expect(mocks.invoke.mock.calls.filter(c => c[0] === 'launch')).toEqual([['launch', { id: 'stable-id' }]])
  })
  it('answers a launch prompt even while Play is still waiting for a reply', async () => {
    let prompt: any = undefined
    let notify = () => {}
    mocks.connect.mockImplementation(async (callback: () => void) => { notify = callback })
    mocks.invoke.mockImplementation(async (method: string) => {
      if (method === 'launch') return new Promise(() => {})  // Play's reply has not come back
      if (method === 'answerPrompt') prompt = undefined
      return method === 'snapshot' ? { instances: [instance], accounts: [], prompt } : undefined
    })
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    await button('▶ Play').trigger('click'); await flushPromises()
    prompt = { id: 'p1', title: 'Offline player name', text: 'Choose a name', choices: ['Cancel', 'Continue'], input: 'Steve', textInput: true }
    notify(); await flushPromises()
    await wrapper.find('[data-choice=Continue]').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('answerPrompt', { promptId: 'p1', choice: 'Continue', input: 'Steve' })
    expect(wrapper.find('.dialog-compact').exists()).toBe(false)
  })
  it('shows a failure that happened between two refreshes, but not one from before the page opened', async () => {
    let launches: any = { 'stable-id': { active: false, failed: true, status: 'Old failure', startedAt: Date.now() - 60000 } }
    let notify = () => {}
    mocks.connect.mockImplementation(async (callback: () => void) => { notify = callback })
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [instance], accounts: [], launches } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    expect(wrapper.find('.launch-overlay').exists()).toBe(false)
    launches = { 'stable-id': { active: false, failed: true, status: 'Proxy connection refused', startedAt: Date.now() + 1 } }
    notify(); await flushPromises()
    expect(wrapper.find('.launch-overlay [role=alert]').attributes('data-reason')).toBe('network')
    await wrapper.find('[data-action=launch-close]').trigger('click')
    notify(); await flushPromises()
    expect(wrapper.find('.launch-overlay').exists()).toBe(false)
  })
  it('turns Prism\'s technical launch failures into a plain reason, in English and Portuguese', async () => {
    let launch: any = { active: true, status: 'Loading components' }, language = 'en_US'
    let notify = () => {}
    mocks.connect.mockImplementation(async (callback: () => void) => { notify = callback })
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [instance], accounts: [], language, launches: { 'stable-id': launch } } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    const reason = () => wrapper!.find('.launch-overlay [role=alert]')
    launch = { active: false, failed: true, status: 'Instance update failed because: Component metadata update task failed while downloading from remote server:\nCould not download metadata for Minecraft 1.21.1. Please change the version or try again later.' }
    notify(); await flushPromises()
    expect(reason().attributes('data-reason')).toBe('network')
    expect(reason().text()).not.toContain('change the version')
    for (const action of ['launch-retry', 'launch-log']) expect(wrapper.find(`[data-action=${action}]`).exists()).toBe(true)
    language = 'pt_BR'; notify(); await flushPromises()
    expect(reason().text()).toBe('Não foi possível conectar aos servidores do Minecraft. Verifique sua conexão e tente de novo.')
    language = 'en_US'; launch = { active: true, status: 'Preparing launch' }; notify(); await flushPromises()
    launch = { active: false, failed: true, status: 'Pre-Launch command failed with code 1.' }
    notify(); await flushPromises()
    expect(reason().attributes('data-reason')).toBe('preLaunch')
    expect(reason().text()).toContain('(code 1)')
  })
  it('reports bridge errors as dismissible toasts without the raw Error prefix', async () => {
    mocks.connect.mockResolvedValue(undefined)
    mocks.invoke.mockImplementation(async (method: string) => {
      if (method === 'launch') throw new Error('Select a signed-in account in Accounts first')
      return method === 'snapshot' ? { instances: [instance], accounts: [] } : undefined
    })
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    await button('▶ Play').trigger('click'); await flushPromises()
    expect(wrapper.find('.toast').text()).toContain('Select a signed-in account in Accounts first')
    expect(wrapper.find('.toast').text()).not.toContain('Error:')
    await wrapper.find('.toast button').trigger('click')
    expect(wrapper.find('.toast').exists()).toBe(false)
  })
  it('opens on the most recently played instance with its playtime', async () => {
    const day = 86400000
    const older = { ...instance, id: 'older', name: 'Older pack', lastLaunch: Date.now() - 3 * day, playTime: 600 }
    const recent = { ...instance, id: 'recent', name: 'Recent pack', lastLaunch: Date.now(), playTime: 3 * 3600 + 15 * 60 }
    mocks.connect.mockResolvedValue(undefined)
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [older, recent], accounts: [] } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    expect(wrapper.find('.bp-hero h1').text()).toBe('Recent pack')
    expect(wrapper.find('.bp-hero .eyebrow').text()).toBe('Continue playing')
    expect(wrapper.find('.bp-hero').text()).toContain('3 h 15 min played')
    expect(wrapper.find('.bp-hero').text()).toContain('Played today')
    expect(wrapper.findAll('.instance-card strong').map(c => c.text())).toEqual(['Recent pack', 'Older pack'])
  })
  it('announces a finished background task once', async () => {
    let task: any = { active: true, title: 'Add CrazyCraft to Steam', status: 'Adding CrazyCraft to Steam…' }
    let notify = () => {}
    mocks.connect.mockImplementation(async (callback: () => void) => { notify = callback })
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [instance], accounts: [], task } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    expect(wrapper.find('.toast').exists()).toBe(false)
    task = { active: false, title: 'Add CrazyCraft to Steam', status: 'Complete' }
    notify(); await flushPromises(); notify(); await flushPromises()
    expect(wrapper.findAll('.toast-ok').map(t => t.text())).toEqual(['Add CrazyCraft to Steam: done×'])
  })
  it('shows the launch steps, honest timing and a stall message instead of looking frozen', async () => {
    const now = Date.now()
    mocks.connect.mockResolvedValue(undefined)
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? {
      instances: [{ ...instance, launching: true, canLaunch: false, running: true, launchSeconds: 40 }], accounts: [],
      launches: { 'stable-id': { active: true, stage: 'files', status: 'Downloading libraries', startedAt: now - 60000, stageAt: now - 30000, changedAt: now - 20000 } },
    } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    const overlay = wrapper.find('.launch-overlay')
    expect(overlay.findAll('.launch-steps li').map(li => li.classes())).toEqual([['done'], ['current'], []])
    expect(overlay.find('[aria-current=step]').text()).toContain('Preparing game files')
    expect(overlay.find('.launch-timing').text()).toBe('1:00 · usually ready in about 40 s')
    expect(overlay.find('.launch-stall').text()).toContain('Still working on game files')
    expect(overlay.find('[data-action=launch-hide]').exists()).toBe(true)
  })
  it('tells a crash apart from a failed launch and shows the end of the log', async () => {
    let launch: any = { active: true, stage: 'running', status: 'Running' }
    let notify = () => {}
    mocks.connect.mockImplementation(async (callback: () => void) => { notify = callback })
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [{ ...instance, running: launch.active }], accounts: [], launches: { 'stable-id': launch } } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    launch = { active: false, failed: true, crashed: true, status: 'Game crashed.', logTail: ['[Render thread/ERROR] Missing texture', 'Exit code 1'] }
    notify(); await flushPromises()
    expect(wrapper.find('.launch-overlay .eyebrow').text()).toBe('The game crashed')
    expect(wrapper.find('.launch-tail').text()).toContain('Missing texture')
    expect(wrapper.find('[data-action=launch-retry]').exists()).toBe(true)
  })
  it('says when the game closed and for how long it ran', async () => {
    const start = Date.now() - 3900000
    let launch: any = { active: true, stage: 'running', status: 'Running', runningAt: start }
    let notify = () => {}
    mocks.connect.mockImplementation(async (callback: () => void) => { notify = callback })
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [instance], accounts: [], launches: { 'stable-id': launch } } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    launch = { active: false, stage: 'closed', status: 'Game exited', runningAt: start, endedAt: start + 3900000 }
    notify(); await flushPromises()
    expect(wrapper.find('.toast-ok').text()).toContain('CrazyCraft closed after 1 h 5 min')
    expect(wrapper.find('.launch-overlay').exists()).toBe(false)
  })
  it('offers the way back to a running game', async () => {
    mocks.connect.mockResolvedValue(undefined)
    mocks.invoke.mockImplementation(async (method: string) => method === 'snapshot' ? { instances: [{ ...instance, running: true, gameWindow: true }], accounts: [] } : undefined)
    wrapper = mount(App, { attachTo: document.body }); await flushPromises()
    expect(wrapper.find('.running-banner').text()).toContain('CrazyCraft is running')
    await wrapper.find('[data-action=back-to-game]').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('backToGame', {})
  })
})
