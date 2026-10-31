// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import AccountPanel from './AccountPanel.vue'
const mocks = vi.hoisted(() => ({ invoke: vi.fn() }))
vi.mock('./bridge', () => mocks)
describe('account management confirmation', () => {
  let wrapper: ReturnType<typeof mount>
  afterEach(() => { wrapper?.unmount(); vi.resetAllMocks() })
  const button = (label: string) => wrapper.findAll('button').find(b => b.text() === label)!
  async function setup() {
    mocks.invoke.mockImplementation(async (method: string) => method === 'accountDetails'
      ? { name: 'TestPlayer', skin: '', cape: '', capes: [{ id: 'owned-cape', name: 'Owned cape' }] } : {})
    wrapper = mount(AccountPanel, { props: { account: 'test-id' }, attachTo: document.body })
    await flushPromises()
  }
  it('does not remove an account until the exact name is confirmed', async () => {
    await setup(); await button('Sign out').trigger('click'); await flushPromises()
    expect(button('Sign out of this launcher').attributes('disabled')).toBeDefined()
    await wrapper.find('input').setValue('WrongName')
    expect(button('Sign out of this launcher').attributes('disabled')).toBeDefined()
    expect(mocks.invoke.mock.calls.some(c => c[0] === 'removeAccount')).toBe(false)
    await wrapper.find('input').setValue('TestPlayer')
    await button('Sign out of this launcher').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('removeAccount', { account: 'test-id', confirmName: 'TestPlayer' })
  })
  it('requires a separate apply action after selecting a cape', async () => {
    await setup(); await button('Owned cape').trigger('click')
    expect(mocks.invoke.mock.calls.some(c => c[0] === 'changeCape')).toBe(false)
    await button('Apply cape selection').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('changeCape', { account: 'test-id', cape: 'owned-cape' })
  })
})
