// 状态
let isScanning = false;
let devices = [];
let eventSource = null;

// DOM 元素
const scanBtn = document.getElementById('scanBtn');
const stopBtn = document.getElementById('stopBtn');
const scanStatus = document.getElementById('scanStatus');
const deviceCount = document.getElementById('deviceCount');
const deviceList = document.getElementById('deviceList');
const toast = document.getElementById('toast');

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    connectSSE();
    loadDevices();

    scanBtn.addEventListener('click', startScan);
    stopBtn.addEventListener('click', stopScan);
});

// 连接 SSE
function connectSSE() {
    if (eventSource) {
        eventSource.close();
    }

    eventSource = new EventSource('/api/events');

    eventSource.addEventListener('connected', () => {
        console.log('SSE connected');
    });

    eventSource.addEventListener('device_found', (e) => {
        const data = JSON.parse(e.data);
        addOrUpdateDevice(data.device);
    });

    eventSource.addEventListener('scan_complete', () => {
        updateScanState(false);
        showToast('扫描完成', 'success');
    });

    eventSource.addEventListener('pair_success', (e) => {
        const data = JSON.parse(e.data);
        showToast('配对成功: ' + data.device.name, 'success');
        updateDevicePairStatus(data.device.address, true);
    });

    eventSource.addEventListener('pair_failed', (e) => {
        showToast('配对失败', 'error');
    });

    eventSource.addEventListener('unpair_success', (e) => {
        const data = JSON.parse(e.data);
        showToast('已取消配对: ' + data.device.name, 'success');
        updateDevicePairStatus(data.device.address, false);
    });

    eventSource.onerror = () => {
        console.log('SSE error, reconnecting...');
        setTimeout(connectSSE, 3000);
    };
}

// 开始扫描
async function startScan() {
    try {
        const response = await fetch('/api/scan', { method: 'POST' });
        const data = await response.json();

        if (data.success) {
            updateScanState(true);
            devices = [];
            renderDevices();
            showToast('开始扫描...', 'success');
        } else {
            showToast('启动扫描失败', 'error');
        }
    } catch (error) {
        console.error('Scan error:', error);
        showToast('扫描失败', 'error');
    }
}

// 停止扫描
async function stopScan() {
    try {
        const response = await fetch('/api/scan/stop', { method: 'POST' });
        const data = await response.json();

        if (data.success) {
            updateScanState(false);
            showToast('扫描已停止', 'success');
        }
    } catch (error) {
        console.error('Stop scan error:', error);
    }
}

// 加载设备列表
async function loadDevices() {
    try {
        const response = await fetch('/api/devices');
        const data = await response.json();

        devices = data.devices || [];
        renderDevices();

        if (data.scanState === 'scanning') {
            updateScanState(true);
        }
    } catch (error) {
        console.error('Load devices error:', error);
    }
}

// 更新扫描状态
function updateScanState(scanning) {
    isScanning = scanning;

    scanBtn.disabled = scanning;
    stopBtn.disabled = !scanning;

    if (scanning) {
        scanStatus.textContent = '状态: 扫描中...';
        document.body.classList.add('scanning');
    } else {
        scanStatus.textContent = '状态: 空闲';
        document.body.classList.remove('scanning');
    }
}

// 添加或更新设备
function addOrUpdateDevice(device) {
    const index = devices.findIndex(d => d.address === device.address);

    if (index >= 0) {
        devices[index] = device;
    } else {
        devices.push(device);
    }

    renderDevices();
}

// 更新设备配对状态
function updateDevicePairStatus(address, paired) {
    const device = devices.find(d => d.address === address);
    if (device) {
        device.paired = paired;
        renderDevices();
    }
}

// 创建设备元素
function createDeviceElement(device) {
    const item = document.createElement('div');
    item.className = 'device-item' + (device.paired ? ' paired' : '');

    const info = document.createElement('div');
    info.className = 'device-info';

    const name = document.createElement('div');
    name.className = 'device-name';
    name.textContent = device.name;

    if (device.paired) {
        const badge = document.createElement('span');
        badge.className = 'paired-badge';
        badge.textContent = '已配对';
        name.appendChild(badge);
    }

    const address = document.createElement('div');
    address.className = 'device-address';
    address.textContent = device.address;

    const meta = document.createElement('div');
    meta.className = 'device-meta';

    const rssi = document.createElement('span');
    rssi.className = 'rssi ' + getRssiClass(device.rssi);
    rssi.textContent = '信号: ' + device.rssi + ' dBm';

    const type = document.createElement('span');
    type.textContent = '类型: ' + getDeviceType(device.cod);

    meta.appendChild(rssi);
    meta.appendChild(type);

    info.appendChild(name);
    info.appendChild(address);
    info.appendChild(meta);

    const actions = document.createElement('div');
    actions.className = 'device-actions';

    const btn = document.createElement('button');
    btn.className = device.paired ? 'btn btn-unpair' : 'btn btn-pair';
    btn.textContent = device.paired ? '取消配对' : '配对';
    btn.onclick = function() {
        if (device.paired) {
            unpairDevice(device.address);
        } else {
            pairDevice(device.address);
        }
    };

    actions.appendChild(btn);
    item.appendChild(info);
    item.appendChild(actions);

    return item;
}

// 渲染设备列表
function renderDevices() {
    deviceList.innerHTML = '';

    if (devices.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'empty-state';

        const p = document.createElement('p');
        p.textContent = '点击"扫描设备"开始搜索附近的蓝牙设备';
        empty.appendChild(p);

        deviceList.appendChild(empty);
        deviceCount.textContent = '已发现: 0 台设备';
        return;
    }

    deviceCount.textContent = '已发现: ' + devices.length + ' 台设备';

    devices.forEach(device => {
        deviceList.appendChild(createDeviceElement(device));
    });
}

// 配对设备
async function pairDevice(address) {
    try {
        showToast('正在配对...', 'success');

        const response = await fetch('/api/pair', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });

        const data = await response.json();

        if (!data.success) {
            showToast('配对失败', 'error');
        }
    } catch (error) {
        console.error('Pair error:', error);
        showToast('配对失败', 'error');
    }
}

// 取消配对
async function unpairDevice(address) {
    try {
        const response = await fetch('/api/unpair', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });

        const data = await response.json();

        if (data.success) {
            showToast('已取消配对', 'success');
            updateDevicePairStatus(address, false);
        } else {
            showToast('取消配对失败', 'error');
        }
    } catch (error) {
        console.error('Unpair error:', error);
        showToast('取消配对失败', 'error');
    }
}

// 获取 RSSI 样式类
function getRssiClass(rssi) {
    if (rssi >= -50) return 'excellent';
    if (rssi >= -60) return 'good';
    if (rssi >= -70) return 'fair';
    if (rssi >= -80) return 'weak';
    return 'very-weak';
}

// 获取设备类型
function getDeviceType(cod) {
    const majorClasses = {
        0x00: '杂项',
        0x01: '电脑',
        0x02: '手机',
        0x03: '网络设备',
        0x04: '音视频',
        0x05: '外设',
        0x06: '成像设备',
        0x07: '穿戴设备',
        0x08: '玩具',
        0x09: '健康设备'
    };

    const major = (cod >> 8) & 0x1F;
    return majorClasses[major] || '未知';
}

// 显示提示
function showToast(message, type) {
    toast.textContent = message;
    toast.className = 'toast show ' + (type || '');

    setTimeout(function() {
        toast.className = 'toast';
    }, 3000);
}