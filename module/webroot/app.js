import { exec, getPackagesInfo } from './ksu.js';

const modulePath = '/data/adb/modules/zygisk_modmenu_template';
const validProcess = /^[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+(?::[A-Za-z0-9_.]+)?$/;
const element = id => document.getElementById(id);
const quote = text => "'" + text.replace(/'/g, "'\\''") + "'";
const unique = values => [...new Set(values)].sort();
const selected = new Set();
let installed = [];
let labels = new Map();
let saved = '';
let savedDelay = 15;
let loaded = false;
let busy = false;

const providerStates = {
  builtin: ['Magisk tích hợp', 'Đang dùng Zygisk có sẵn của Magisk.', 'ok'],
  zygisksu: ['Zygisk Next', 'Đã cài provider Zygisk Next.', 'ok'],
  rezygisk: ['ReZygisk', 'Đã cài provider ReZygisk.', 'ok'],
  neozygisk: ['NeoZygisk', 'Đã cài provider NeoZygisk.', 'ok'],
  'ready-magisk': ['Chưa có provider', 'Có thể tải Zygisk Next từ bản phát hành chính thức.', 'warn'],
  'ready-ksu': ['Chưa có provider', 'Có thể tải Zygisk Next từ bản phát hành chính thức.', 'warn'],
  'old-magisk': ['Magisk chưa đủ phiên bản', 'Zygisk Next yêu cầu Magisk 26402 trở lên. Có thể bật Zygisk tích hợp nếu trình quản lý hỗ trợ.', 'warn'],
  disabled: ['Provider chưa sẵn sàng', 'Kiểm tra module provider trong trình quản lý root.', 'warn'],
  conflict: ['Xung đột provider', 'Chỉ bật một provider Zygisk, sau đó khởi động lại.', 'warn'],
  'multiple-root': ['Nhiều trình root', 'Zygisk Next yêu cầu chỉ có một trình quản lý root.', 'warn'],
  unsupported: ['Chưa hỗ trợ', 'Kiểm tra phiên bản Android và trình quản lý root.', 'warn'],
  unknown: ['Chưa xác định', 'Không đọc được trạng thái Zygisk.', 'warn'],
};

function status(message, error = false) {
  element('status').textContent = message;
  element('status').dataset.error = String(error);
}

async function shell(command) {
  const result = await exec(command);
  if (Number(result.errno) !== 0) {
    const output = (result.stderr || result.stdout || '').trim();
    throw new Error(output.split(/\r?\n/).slice(-2).join(' ') || `Lệnh thất bại (${result.errno}).`);
  }
  return result.stdout || '';
}

function draft() {
  return unique([...selected, ...element('extra').value.split(/\s+/).filter(Boolean)]);
}

function validatedSelection() {
  const names = draft();
  if (names.length > 1024 || names.join('\n').length > 60000) {
    throw new Error('Danh sách tiến trình quá dài.');
  }
  const invalid = names.find(name => !validProcess.test(name));
  if (invalid) throw new Error(`Tên tiến trình không hợp lệ: ${invalid}`);
  return names;
}

function validatedDelay() {
  const raw = element('delay-seconds').value.trim();
  if (!/^\d+$/.test(raw)) throw new Error('Độ trễ phải là số nguyên từ 0 đến 180 giây.');
  const sec = Number(raw);
  if (!Number.isInteger(sec) || sec < 0 || sec > 180) {
    throw new Error('Độ trễ phải nằm trong khoảng 0 đến 180 giây.');
  }
  return sec;
}

function delayState() {
  const raw = element('delay-seconds').value.trim();
  const valid = /^\d+$/.test(raw) && Number(raw) >= 0 && Number(raw) <= 180;
  return { valid, value: valid ? Number(raw) : null, dirty: valid && Number(raw) !== savedDelay };
}

function updateControls() {
  const names = draft();
  const d = delayState();
  const changed = names.join('\n') !== saved || d.dirty;
  element('refresh').disabled = busy;
  element('save').disabled = busy || !loaded || !changed || !d.valid;
  element('clear').disabled = busy || !loaded || (!names.length && !saved);
  element('launch').disabled = busy || !loaded || !names.length || !d.valid;
  element('extra').disabled = busy || !loaded;
  element('delay-seconds').disabled = busy || !loaded;
  element('provider-install').disabled = busy;
  element('apps').inert = busy;
  element('apps').setAttribute('aria-busy', String(busy || !loaded));
}

async function run(task) {
  busy = true;
  updateControls();
  try { await task(); }
  catch (error) { status(error.message || String(error), true); }
  finally { busy = false; updateControls(); }
}

function renderSelection() {
  const names = draft();
  element('selected-count').textContent = names.length;
  const list = element('selection');
  list.replaceChildren();
  for (const name of names) {
    const item = document.createElement('li');
    item.textContent = labels.get(name) || name;
    list.append(item);
  }
  if (!names.length) {
    const empty = document.createElement('li');
    empty.className = 'empty';
    empty.textContent = 'Chưa chọn ứng dụng.';
    list.append(empty);
  }
  updateControls();
}

function selectionChanged() {
  renderSelection();
  const d = delayState();
  if (!d.valid) {
    status('Độ trễ không hợp lệ (nhập 0–180 giây).', true);
    return;
  }
  const changed = draft().join('\n') !== saved || d.dirty;
  status(changed ? 'Có thay đổi chưa lưu.' : 'Cấu hình đã lưu.');
}

function renderApps() {
  const query = element('filter').value.trim().toLocaleLowerCase('vi');
  const list = element('apps');
  const items = document.createDocumentFragment();
  const matching = installed.filter(name =>
    name.toLowerCase().includes(query) || (labels.get(name) || '').toLocaleLowerCase('vi').includes(query));
  element('app-count').textContent = query ? `${matching.length}/${installed.length}` : installed.length;

  for (const name of matching) {
    const label = document.createElement('label');
    label.className = 'app';
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = selected.has(name);
    box.addEventListener('change', () => {
      if (box.checked) selected.add(name);
      else selected.delete(name);
      selectionChanged();
    });
    const details = document.createElement('span');
    const appName = document.createElement('span');
    appName.className = 'app-name';
    appName.textContent = labels.get(name) || name;
    details.append(appName);
    if (appName.textContent !== name) {
      const packageName = document.createElement('span');
      packageName.className = 'app-package';
      packageName.textContent = name;
      details.append(packageName);
    }
    label.append(box, details);
    items.append(label);
  }
  if (!matching.length) {
    const empty = document.createElement('p');
    empty.className = 'empty';
    empty.textContent = query ? 'Không có ứng dụng phù hợp.' : 'Chưa có ứng dụng người dùng.';
    items.append(empty);
  }
  list.replaceChildren(items);
}

function applySelection(names) {
  const packages = new Set(installed);
  selected.clear();
  for (const name of names) if (packages.has(name)) selected.add(name);
  element('extra').value = names.filter(name => !packages.has(name)).join('\n');
  renderApps();
  renderSelection();
}

async function refreshProvider() {
  let state;
  try { state = (await shell(`sh ${modulePath}/zygisk-provider.sh status`)).trim(); }
  catch { state = 'unknown'; }
  const pending = state.endsWith('-pending');
  const [title, description, tone] = providerStates[state] ||
    (pending ? ['Chờ khởi động lại', 'Provider đã được cài. Khởi động lại Android để áp dụng.', 'warn']
      : providerStates.unknown);
  const badge = element('provider-badge');
  badge.textContent = title;
  badge.dataset.tone = tone;
  element('provider-message').textContent = description;
  element('provider-message').hidden = false;
  element('provider-install').hidden = !state.startsWith('ready-');
}

async function readLabels(names) {
  if (!names.length) return new Map();
  if (typeof globalThis.ksu?.getPackagesInfo === 'function') {
    try {
      const info = await getPackagesInfo(names);
      if (Array.isArray(info) && info.length) {
        return new Map(info.map(item => [item.packageName, item.appLabel || item.label])
          .filter(([name, label]) => name && typeof label === 'string'));
      }
    } catch {
      // Standalone and older managers can read labels via the packaged helper.
    }
  }
  try {
    const args = names.map(quote).join(' ');
    const json = await shell(`CLASSPATH=${modulePath}/webui/app-labels.dex app_process /system/bin dev.libtool.webui.AppLabels ${args}`);
    return new Map(Object.entries(JSON.parse(json)));
  } catch {
    return new Map();
  }
}

async function refresh() {
  status('Đang đọc cấu hình…');
  const current = draft();
  const currentDelay = element('delay-seconds').value;
  const edited = loaded && current.join('\n') !== saved;
  const delayEdited = loaded && delayState().dirty;
  const [packages, targets, properties, delayRaw] = await Promise.all([
    shell('pm list packages -3'),
    shell(`cat ${modulePath}/targets.txt`),
    shell(`cat ${modulePath}/module.prop`),
    shell(`sh ${modulePath}/configure.sh --get-delay`),
  ]);
  const parsedDelay = /^\d+$/.test(delayRaw.trim()) ? Math.min(180, Math.max(0, Number(delayRaw.trim()))) : 15;
  savedDelay = parsedDelay;
  if (!delayEdited) element('delay-seconds').value = String(savedDelay);
  else element('delay-seconds').value = currentDelay;
  const listed = unique(packages.split(/\r?\n/).map(line => line.replace(/^package:/, '').trim())
    .filter(name => validProcess.test(name)));
  const version = properties.match(/^version=(.+)$/m)?.[1]?.trim();
  if (version) {
    element('version').textContent = version;
    element('version').hidden = false;
  }
  labels = await readLabels(listed);
  installed = listed.sort((a, b) => (labels.get(a) || a).localeCompare(labels.get(b) || b, 'vi'));
  const activeNames = unique(targets.split(/\r?\n/).map(line => line.split('#')[0].trim())
    .filter(name => validProcess.test(name)));
  saved = activeNames.join('\n');
  loaded = true;
  applySelection(edited ? current : activeNames);
  selectionChanged();
  await refreshProvider();
}

async function save(names, delaySec = savedDelay) {
  const args = names.length ? names.map(quote).join(' ') : '--clear';
  await shell(`sh ${modulePath}/configure.sh ${args}`);
  if (delaySec !== savedDelay) {
    await shell(`sh ${modulePath}/configure.sh --delay ${delaySec}`);
    savedDelay = delaySec;
  }
  saved = names.join('\n');
  element('delay-seconds').value = String(savedDelay);
  applySelection(names);
  status('Đã lưu cấu hình. Mở lại ứng dụng để áp dụng.');
}

element('filter').addEventListener('input', renderApps);
element('extra').addEventListener('input', selectionChanged);
element('delay-seconds').addEventListener('input', selectionChanged);
element('refresh').addEventListener('click', () => run(refresh));
element('save').addEventListener('click', () => run(() => save(validatedSelection(), validatedDelay())));
element('clear').addEventListener('click', () => run(() => save([], savedDelay)));
element('launch').addEventListener('click', () => run(async () => {
  const names = validatedSelection();
  const delaySec = validatedDelay();
  const apps = unique(names.map(name => name.split(':')[0]));
  if (apps.length !== 1) throw new Error('Chọn đúng một ứng dụng để mở.');
  await save(names, delaySec);
  await shell(`monkey -p ${quote(apps[0])} -c android.intent.category.LAUNCHER 1`);
  status('Đã gửi yêu cầu mở ứng dụng.');
}));
element('provider-install').addEventListener('click', () => run(async () => {
  status('Đang cài Zygisk Next…');
  try {
    const output = await shell(`sh ${modulePath}/zygisk-provider.sh install`);
    status(output.trim().split(/\r?\n/).pop());
  } finally {
    await refreshProvider();
  }
}));

if (typeof globalThis.ksu?.exec === 'function') run(refresh);
else {
  status('Mở trang từ KernelSU hoặc KsuWebUI Standalone với quyền root.', true);
  element('apps').setAttribute('aria-busy', 'false');
}
