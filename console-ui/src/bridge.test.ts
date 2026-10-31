// @vitest-environment happy-dom
import { afterEach, describe, expect, it, vi } from 'vitest'
describe('native bridge boundary', () => {
  afterEach(() => { delete window.qt; delete window.QWebChannel; vi.useRealTimers(); vi.resetModules() })
  it('fails closed outside the native launcher, without inventing a library', async () => {
    const { connect, invoke } = await import('./bridge')
    await expect(connect(() => {})).rejects.toThrow('No launcher is connected')
    await expect(invoke('snapshot')).rejects.toThrow('Prism is not connected')
  })
  it('propagates native errors and never retries a mutation automatically', async () => {
    const request = vi.fn((_method, _args, callback) => callback({ ok: false, error: 'Instance no longer exists' }))
    window.qt = { webChannelTransport: {} }
    window.QWebChannel = class { constructor(_transport: unknown, callback: Function) { callback({ objects: { prism: { request, changed: { connect() {} } } } }) } } as never
    const { connect, invoke } = await import('./bridge')
    await connect(() => {})
    await expect(invoke('saveInstance', { id: 'removed' })).rejects.toThrow('Instance no longer exists')
    expect(request).toHaveBeenCalledTimes(1)
  })
  it('times out a dead native process instead of leaving controls pending forever', async () => {
    vi.useFakeTimers()
    window.qt = { webChannelTransport: {} }
    window.QWebChannel = class { constructor(_transport: unknown, callback: Function) { callback({ objects: { prism: { request() {}, changed: { connect() {} } } } }) } } as never
    const { connect, invoke } = await import('./bridge')
    await connect(() => {})
    const result = expect(invoke('snapshot')).rejects.toThrow('did not respond')
    await vi.advanceTimersByTimeAsync(30001)
    await result
  })
})
