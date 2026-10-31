<script setup lang="ts">
import { t } from './i18n'
import { onMounted, ref } from 'vue'
import { invoke } from './bridge'
const props = defineProps<{ kind?: string }>()
const emit = defineEmits<{ select: [path: string] }>()
const data = ref<{ path: string; parent: string; entries: { path: string; name: string; directory: boolean }[] }>({ path: '', parent: '', entries: [] })
const busy = ref(false), error = ref(''), path = ref('')
async function browse(target?: string) {
  if (busy.value) return
  busy.value = true; error.value = ''
  try { data.value = await invoke('browseFiles', { ...(target ? { path: target } : {}), kind: props.kind }); path.value = data.value.path }
  catch (e) { error.value = String(e) } finally { busy.value = false }
}
onMounted(() => browse())
</script>
<template>
  <section :aria-label="t('files.choose_local_archive')" :aria-busy="busy">
    <p v-if="error" role="alert">{{ error }}</p>
    <label>{{ t('files.folder') }}<input v-model="path" @keydown.enter="browse(path)"></label>
    <div class="actions"><button :disabled="busy" @click="browse(path)">{{ t('files.open_folder') }}</button><button :disabled="busy || !data.parent" @click="browse(data.parent)">{{ t('files.parent_folder') }}</button></div>
    <div class="resource-list"><button v-for="file in data.entries" :key="file.path" class="row" :disabled="busy" @click="file.directory ? browse(file.path) : emit('select', file.path)"><span>{{ file.directory ? '▣' : '◇' }} {{ file.name }}</span><span>{{ file.directory ? t('common.open') : t('common.select') }}</span></button></div>
    <p v-if="!busy && !data.entries.length">{{ t('files.no_compatible_archives_or_folders') }}</p>
  </section>
</template>
