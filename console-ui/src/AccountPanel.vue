<script setup lang="ts">
import { t } from './i18n'
import { nextTick, onMounted, ref, watch } from 'vue'
import { invoke } from './bridge'
import FilePicker from './FilePicker.vue'
const props = defineProps<{ account: string }>()
const emit = defineEmits<{ changed: []; removed: [] }>()
const details = ref<{ name: string; skin: string; capes: { id: string; name: string }[]; cape: string }>({ name: '', skin: '', capes: [], cape: '' })
const root = ref<HTMLElement | null>(null)
const busy = ref(false), error = ref(''), status = ref(''), path = ref(''), variant = ref('classic'), cape = ref(''), confirming = ref(''), confirmName = ref('')
watch(confirming, async () => { await nextTick(); root.value?.querySelector<HTMLElement>('input, button:not(:disabled)')?.focus() })
async function run(job: () => Promise<void>) {
  if (busy.value) return
  busy.value = true; error.value = ''; status.value = ''
  try { await job(); emit('changed') } catch (e) { error.value = String(e) } finally { busy.value = false }
}
async function refresh() { details.value = await invoke('accountDetails', { account: props.account }); cape.value = details.value.cape }
onMounted(() => run(refresh))
async function appearance(method: string, args: Record<string, unknown>) {
  await invoke(method, { account: props.account, ...args }); confirming.value = ''; status.value = t('account.requestStarted')
}
</script>
<template>
  <section ref="root" :aria-busy="busy">
    <p v-if="error" role="alert">{{ error }}</p><p v-if="status" role="status">{{ status }}</p>
    <h3>{{ details.name }}</h3>
    <p>{{ t('account.skin_and_cape_changes_update') }}</p>
    <template v-if="confirming === 'remove'">
      <label>{{ t('account.typeToSignOut', { name: details.name }) }}<input v-model="confirmName" autocomplete="off"></label>
      <button @click="confirming = ''">{{ t('account.keep_account') }}</button>
      <button :disabled="busy || confirmName !== details.name" @click="run(async () => { await invoke('removeAccount', { account, confirmName }); emit('removed') })">{{ t('account.sign_out_of_this_launcher') }}</button>
    </template>
    <template v-else-if="confirming === 'reset'">
      <p>{{ t('account.reset_this_account_to_its') }}</p><button @click="confirming = ''">{{ t('account.keep_skin') }}</button><button :disabled="busy" @click="run(() => appearance('resetSkin', { confirmed: true }))">{{ t('account.reset_skin') }}</button>
    </template>
    <template v-else-if="confirming === 'upload'">
      <FilePicker kind="skin" @select="path = $event" />
      <p v-if="path">{{ path }}</p>
      <div class="actions"><button v-for="model in ['classic', 'slim']" :key="model" :aria-pressed="variant === model" @click="variant = model">{{ model === 'classic' ? t('account.classicArms') : t('account.slimArms') }}</button></div>
      <button @click="confirming = ''">{{ t('account.cancel') }}</button><button :disabled="busy || !path" @click="run(() => appearance('uploadSkin', { path, variant }))">{{ t('account.upload_selected_skin') }}</button>
    </template>
    <template v-else>
      <div class="actions"><button @click="confirming = 'upload'">{{ t('account.upload_png_skin') }}</button><button @click="confirming = 'reset'">{{ t('account.reset_skin') }}</button><button @click="confirming = 'remove'">{{ t('account.sign_out') }}</button><button :disabled="busy" @click="run(refresh)">{{ t('account.refresh_account') }}</button></div>
      <h3>{{ t('account.cape') }}</h3><button class="row" :aria-pressed="cape === ''" @click="cape = ''">{{ t('account.no_cape') }}</button>
      <button v-for="c in details.capes" :key="c.id" class="row" :aria-pressed="cape === c.id" @click="cape = c.id">{{ c.name || c.id }}</button>
      <button :disabled="busy || cape === details.cape" @click="run(() => appearance('changeCape', { cape }))">{{ t('account.apply_cape_selection') }}</button>
    </template>
  </section>
</template>
