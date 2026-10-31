// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { describe, expect, it, vi } from 'vitest'
import LauncherSettings from './LauncherSettings.vue'
import { soundSettings } from './sound'
const mocks = vi.hoisted(() => ({ invoke: vi.fn() }))
vi.mock('./bridge', () => mocks)
describe('launcher settings', () => {
  it('shows each option and saves a change at once', async () => {
    const options: Record<string, unknown> = { soundEnabled: true, soundVolume: 30, installDependencies: true, trackMetadata: true, askModpackUpdate: true,
      downloadGameFiles: true, parallelDownloads: 6, retryLimit: 3, timeoutSeconds: 60, recordPlayTime: true, showPlayTime: true, lowMemoryWarning: true,
      about: { name: 'Prism Launcher', version: '10.0.0', commit: 'abc1234', qt: '6.10.1' } }
    mocks.invoke.mockImplementation(async (method: string, args: { key: string; value: unknown }) => {
      if (method === 'launcherOptions') return options
      if (method === 'setLauncherOption') return (options[args.key] = args.value)
    })
    const wrapper = mount(LauncherSettings)
    await flushPromises()
    expect(wrapper.findAll('[role=switch]')).toHaveLength(8)
    expect(wrapper.text()).toContain('Prism Launcher 10.0.0')

    await wrapper.find('[data-option=installDependencies]').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('setLauncherOption', { key: 'installDependencies', value: false })
    expect(wrapper.find('[data-option=installDependencies]').attributes('aria-checked')).toBe('false')

    const parallel = wrapper.find('[data-option=parallelDownloads]')
    await parallel.findAll('button')[1].trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('setLauncherOption', { key: 'parallelDownloads', value: 7 })
    expect(parallel.find('output').text()).toBe('7')

    // Sound changes apply to the interface straight away.
    await wrapper.find('[data-option=soundVolume]').findAll('button')[0].trigger('click'); await flushPromises()
    expect(soundSettings.volume).toBe(20)
    await wrapper.find('[data-option=soundEnabled]').trigger('click'); await flushPromises()
    expect(soundSettings.enabled).toBe(false)
  })

  it('stops steppers at their limits', async () => {
    mocks.invoke.mockImplementation(async (method: string) => method === 'launcherOptions' ? { retryLimit: 0, timeoutSeconds: 120 } : undefined)
    const wrapper = mount(LauncherSettings)
    await flushPromises()
    expect(wrapper.find('[data-option=retryLimit]').findAll('button')[0].attributes('disabled')).toBeDefined()
    expect(wrapper.find('[data-option=timeoutSeconds]').findAll('button')[1].attributes('disabled')).toBeDefined()
  })
})
