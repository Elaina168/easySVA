const PLAY_URL_FIELDS = [
  'playUrl',
  'play_url',
  'previewUrl',
  'preview_url',
  'url',
  'streamUrl',
  'stream_url',
  'flvUrl',
  'flv_url'
]

export function isBrowserPlayableUrl(url) {
  if (typeof url !== 'string') return false
  const value = url.trim()
  return /^(https?:\/\/|wss?:\/\/|\/)/i.test(value)
}

export function extractPlayableUrl(source) {
  const data = source && source.data && typeof source.data === 'object'
    ? source.data
    : source
  if (typeof data === 'string') {
    return isBrowserPlayableUrl(data) ? data.trim() : ''
  }
  if (!data || typeof data !== 'object') return ''
  for (let index = 0; index < PLAY_URL_FIELDS.length; index += 1) {
    const value = data[PLAY_URL_FIELDS[index]]
    if (isBrowserPlayableUrl(value)) return value.trim()
  }
  return ''
}

export function isFlvUrl(url) {
  return typeof url === 'string' && /\.flv(?:[?#]|$)/i.test(url.trim())
}
