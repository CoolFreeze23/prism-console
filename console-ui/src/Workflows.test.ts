// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import Workflows from './Workflows.vue'
const mocks = vi.hoisted(() => ({ invoke: vi.fn() }))
vi.mock('./bridge', () => mocks)
describe('Controller management workflows', () => {
  let wrapper: ReturnType<typeof mount>
  afterEach(() => { wrapper?.unmount(); vi.resetAllMocks() })
  it('imports only after the user supplies a name and source and activates import', async () => {
    mocks.invoke.mockResolvedValue(undefined)
    wrapper = mount(Workflows, { props: { mode: 'import' } }); await flushPromises()
    expect(mocks.invoke).not.toHaveBeenCalled()
    expect(wrapper.find('button.primary').attributes('disabled')).toBeDefined()
    await wrapper.findAll('input')[0].setValue('My pack')
    await wrapper.findAll('input')[1].setValue('/home/deck/Downloads/pack.mrpack')
    await wrapper.find('button.primary').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('importInstance', { name: 'My pack', source: '/home/deck/Downloads/pack.mrpack' })
  })
  it('does not write global settings on load, and preserves boolean types when toggling', async () => {
    mocks.invoke.mockResolvedValue({ AutomaticJavaSwitch: false, MaxMemAlloc: 4096 })
    wrapper = mount(Workflows, { props: { mode: 'preferences' } }); await flushPromises()
    expect(mocks.invoke.mock.calls).toEqual([['globalSettings']])
    await wrapper.findAll('button').find(b => b.text() === 'Java')!.trigger('click')
    await wrapper.findAll('button').find(b => b.text().includes('Select compatible Java automatically'))!.trigger('click')
    await wrapper.find('button.primary').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('saveGlobalSettings', { id: undefined, values: { AutomaticJavaSwitch: true, MaxMemAlloc: 4096 } })
  })
  it('does not include saved worlds in an export without selection', async () => {
    mocks.invoke.mockResolvedValue('/Downloads/test.zip')
    wrapper = mount(Workflows, { props: { mode: 'export', instanceId: 'stable-id' } }); await flushPromises()
    await wrapper.find('button.primary').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('exportInstance', { id: 'stable-id', includeWorlds: false })
  })
})
