// UI strings. English is the source; pt-BR is written separately from each string's meaning (see
// messages/pt-BR.ts), not translated line by line. The language follows Prism's own Language setting,
// then the system language; anything missing falls back to English.
import { ref } from 'vue'
import { en } from './messages/en'
import { ptBR } from './messages/pt-BR'

export type MessageKey = keyof typeof en
const tables: Record<string, Partial<Record<MessageKey, string>>> = { en, 'pt-BR': ptBR }
export const locale = ref<'en' | 'pt-BR'>('en')

/** Picks the UI language from Prism's setting (e.g. "pt_BR") or, when unset, the system language. */
export function chooseLocale(prismLanguage?: string) {
  const wanted = (prismLanguage || navigator.language || 'en').replace('_', '-').toLowerCase()
  locale.value = wanted.startsWith('pt') ? 'pt-BR' : 'en'
}

const nativeKeys = new Map(Object.entries(en).filter(([key]) => key.startsWith('native.')).map(([key, text]) => [text as string, key as MessageKey]))

/** Text that comes from Prism's side (prompts, statuses, refusals): translated when it is one of ours, else shown as is. */
export function tn(text?: string) {
  if (!text) return ''
  const key = nativeKeys.get(text)
  return key ? t(key) : text
}

/** Looks up a string and fills {placeholders}. */
export function t(key: MessageKey, params: Record<string, string | number> = {}) {
  const text = tables[locale.value][key] ?? en[key]
  return text.replace(/\{(\w+)\}/g, (_, name) => String(params[name] ?? `{${name}}`))
}
