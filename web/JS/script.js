/**
 * ESP32 IoT System - Unified JavaScript
 * Updated to match Control Plane and Data Plane implementations
 * Author: IoT Development Team
 * Version: 2.0
 */

class ESP32IoTSystem {
    constructor() {
        this.baseURL = '';  // Empty for same-origin requests
        this.token = localStorage.getItem('esp32_token') || '';
        this.devices = [];
        this.discoveredDevices = [];
        this.isScanning = false;
        this.scanInterval = null;
        this.logData = [];
        this.refreshInterval = null;
        
        this.init();
    }

    init() {
        this.setupEventListeners();
        this.checkAuthentication();
        this.startAutoRefresh();
    }

    // ==================== AUTHENTICATION ====================
    
    async login(username, password) {
        try {
            const response = await fetch('/login', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: `username=${encodeURIComponent(username)}&password=${encodeURIComponent(password)}`
            });

            const data = await response.json();
            
            if (data.success) {
                this.token = data.token;
                localStorage.setItem('esp32_token', this.token);
                this.showNotification('Login successful!', 'success');
                
                // Redirect to home page
                setTimeout(() => {
                    window.location.href = '/home.html';
                }, 1000);
                
                return true;
            } else {
                this.showNotification(data.message || 'Login failed', 'error');
                return false;
            }
        } catch (error) {
            console.error('Login error:', error);
            this.showNotification('Connection error. Please try again.', 'error');
            return false;
        }
    }

    async logout() {
        try {
            await fetch('/logout', {
                method: 'POST',
                headers: {
                    'Authorization': this.token
                }
            });
            
            this.token = '';
            localStorage.removeItem('esp32_token');
            window.location.href = '/login.html';
        } catch (error) {
            console.error('Logout error:', error);
            // Force logout even if server request fails
            this.token = '';
            localStorage.removeItem('esp32_token');
            window.location.href = '/login.html';
        }
    }

    checkAuthentication() {
        const currentPage = window.location.pathname;
        const isLoginPage = currentPage.includes('login.html') || currentPage === '/';
        
        if (!this.token && !isLoginPage) {
            window.location.href = '/login.html';
            return false;
        }
        
        return true;
    }

    // ==================== DEVICE MANAGEMENT ====================
    
    async loadDevices() {
        try {
            const response = await fetch('/api/devices', {
                headers: {
                    'Authorization': this.token
                }
            });

            if (response.ok) {
                const data = await response.json();
                this.devices = data.devices || [];
                this.updateDeviceDisplay();
                this.updateDeviceCount();
            } else {
                console.error('Failed to load devices');
            }
        } catch (error) {
            console.error('Error loading devices:', error);
        }
    }

    async saveDeviceConfig(config) {
        try {
            // First, configure the device if it's from a scan
            if (config.deviceId && this.discoveredDevices.length > 0) {
                // Find the discovered device
                const discoveredDevice = this.discoveredDevices.find(d => d.deviceId === config.deviceId);
                
                if (discoveredDevice) {
                    // Configure the device through the control plane
                    const configResponse = await fetch('/api/configure', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                            'Authorization': this.token
                        },
                        body: JSON.stringify({
                            deviceId: config.deviceId,
                            deviceName: config.deviceName,
                            deviceType: config.deviceType,
                            readInterval: config.readInterval
                        })
                    });

                    const configData = await configResponse.json();
                    
                    if (!configData.success) {
                        this.showNotification(configData.message || 'Failed to configure device', 'error');
                        return false;
                    }
                }
            } else {
                // Manual device addition
                const response = await fetch('/api/devices', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                        'Authorization': this.token
                    },
                    body: JSON.stringify({
                        name: config.deviceName,
                        type: config.deviceType,
                        ip: config.ipAddress,
                        readInterval: config.readInterval,
                        connected: false,
                        configured: true
                    })
                });

                const data = await response.json();
                
                if (!data.success) {
                    this.showNotification(data.message || 'Failed to save configuration', 'error');
                    return false;
                }
            }
            
            this.showNotification('Device configuration saved!', 'success');
            await this.loadDevices();
            setTimeout(() => {
                window.location.href = '/home.html';
            }, 1000);
            
            return true;
        } catch (error) {
            console.error('Error saving device config:', error);
            this.showNotification('Connection error. Please try again.', 'error');
            return false;
        }
    }

    async updateDeviceStatus(deviceId, status) {
        try {
            const response = await fetch('/api/devices/status', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                    'Authorization': this.token
                },
                body: `deviceId=${encodeURIComponent(deviceId)}&status=${encodeURIComponent(status)}&lastSeen=${Date.now()}`
            });

            if (response.ok) {
                await this.loadDevices(); // Refresh device list
            }
        } catch (error) {
            console.error('Error updating device status:', error);
        }
    }

    // ==================== DEVICE SCANNING ====================
    
    async startDeviceScan() {
        if (this.isScanning) return;
        
        this.isScanning = true;
        this.updateScanButton(true);
        this.clearScanResults();
        
        try {
            // Use advanced scan to get device details
            const response = await fetch('/api/scan/advanced', {
                method: 'POST',
                headers: {
                    'Authorization': this.token
                }
            });

            if (response.ok) {
                const data = await response.json();
                this.discoveredDevices = data.devices || [];
                this.displayScanResults(this.discoveredDevices);
            } else {
                this.showNotification('Scan failed', 'error');
            }
        } catch (error) {
            console.error('Error scanning devices:', error);
            this.showNotification('Scan error. Please try again.', 'error');
        } finally {
            this.isScanning = false;
            this.updateScanButton(false);
        }
    }

    async connectToDevice(deviceId) {
        try {
            const device = this.discoveredDevices.find(d => d.deviceId === deviceId);
            if (!device) {
                this.showNotification('Device not found', 'error');
                return;
            }

            this.showNotification(`Connecting to ${device.description}...`, 'info');

            // Store device info for configuration page
            localStorage.setItem('pendingDevice', JSON.stringify(device));
            
            setTimeout(() => {
                window.location.href = '/config.html';
            }, 1000);
        } catch (error) {
            this.showNotification('Connection failed', 'error');
        }
    }

    // ==================== LOG DATA ====================
    
    async loadLogData() {
        try {
            const response = await fetch('/api/logdata', {
                headers: {
                    'Authorization': this.token
                }
            });

            if (response.ok) {
                const data = await response.json();
                // Process sensor data with new structure
                this.logData = this.processSensorData(data.data || []);
                this.updateLogDataDisplay();
            } else {
                console.error('Failed to load log data');
            }
        } catch (error) {
            console.error('Error loading log data:', error);
        }
    }

    processSensorData(rawData) {
        const processedData = [];
        
        rawData.forEach(entry => {
            // Handle new sensor data structure with readings array
            if (entry.readings && Array.isArray(entry.readings)) {
                entry.readings.forEach(reading => {
                    processedData.push({
                        timestamp: entry.timestamp,
                        deviceName: entry.deviceName || 'Unknown Device',
                        type: reading.type,
                        value: `${reading.value} ${reading.unit}`,
                        status: reading.status || 'ok'
                    });
                });
            } else {
                // Handle legacy single sensor data
                processedData.push({
                    timestamp: entry.timestamp,
                    deviceName: entry.deviceName || 'Unknown Device',
                    type: entry.type || 'sensor',
                    value: `${entry.value} ${entry.unit || ''}`,
                    status: entry.status || 'ok'
                });
            }
        });
        
        return processedData;
    }

    // ==================== UI UPDATE METHODS ====================
    
    updateDeviceDisplay() {
        const sensorList = document.getElementById('deviceList');
        const actuatorList = document.getElementById('aktuatorList');
        
        if (!sensorList && !actuatorList) return;

        // Clear existing content
        if (sensorList) sensorList.innerHTML = '';
        if (actuatorList) actuatorList.innerHTML = '';

        // Filter devices by type
        const sensors = this.devices.filter(device => device.type === 'sensor');
        const actuators = this.devices.filter(device => device.type === 'actuator');

        // Update sensor list
        if (sensorList) {
            sensors.forEach(device => {
                sensorList.appendChild(this.createDeviceElement(device, 'sensor'));
            });
        }

        // Update actuator list
        if (actuatorList) {
            actuators.forEach(device => {
                actuatorList.appendChild(this.createDeviceElement(device, 'actuator'));
            });
        }
    }

    createDeviceElement(device, type) {
        const deviceDiv = document.createElement('div');
        deviceDiv.className = 'device-item-home';
        
        const isConnected = device.connected;
        const statusClass = isConnected ? 'status-connected' : 'status-disconnected';
        const statusText = isConnected ? 'connected' : 'disconnected';
        const buttonClass = isConnected ? 'disconnect-btn' : 'connect-btn';
        const buttonText = isConnected ? 'disconnect' : 'connect';
        
        let deviceHTML = `
            <div class="device-header">
                <div class="device-name">
                    ${type === 'actuator' ? '<span class="expand-icon" onclick="iotSystem.toggleExpand(this)">▼</span>' : ''}
                    ${device.name}
                </div>
                <div class="device-status">
                    <span class="status-text ${statusClass}">${statusText}</span>
                    <a class="setting" href="config.html"><span class="settings-icon">⚙️</span></a>
                    <button class="action-btn ${buttonClass}" onclick="iotSystem.toggleConnection(this, '${device.id || device.ip}')">${buttonText}</button>
                </div>
            </div>
        `;
        
        if (type === 'actuator') {
            deviceHTML += `
                <div class="device-details">
                    <div class="action-row">
                        <span class="action-label">Action:</span>
                        <div class="toggle-buttons">
                            <button class="toggle-btn toggle-on" onclick="iotSystem.sendCommand('${device.id || device.ip}', 'on')">ON</button>
                            <button class="toggle-btn toggle-off" onclick="iotSystem.sendCommand('${device.id || device.ip}', 'off')">OFF</button>
                        </div>
                    </div>
                </div>
            `;
        }
        
        deviceDiv.innerHTML = deviceHTML;
        return deviceDiv;
    }

    updateScanButton(isScanning) {
        const scanBtn = document.getElementById('scanBtn');
        if (!scanBtn) return;
        
        const scanText = scanBtn.querySelector('.scan-text');
        if (isScanning) {
            scanBtn.classList.add('scanning');
            scanText.innerHTML = 'Scanning<span class="loading-dots"></span>';
        } else {
            scanBtn.classList.remove('scanning');
            scanText.textContent = 'Scanning';
        }
    }

    clearScanResults() {
        const deviceList = document.getElementById('deviceList');
        if (deviceList) {
            // Clear existing devices but keep empty state
            const existingDevices = deviceList.querySelectorAll('.device-item');
            existingDevices.forEach(item => item.remove());
        }
        
        // Hide empty state during scan
        const emptyState = document.getElementById('emptyState');
        if (emptyState) {
            emptyState.style.display = 'none';
        }
    }

    displayScanResults(devices) {
        const deviceList = document.getElementById('deviceList');
        const emptyState = document.getElementById('emptyState');
        
        if (!deviceList) return;
        
        if (devices.length === 0) {
            if (emptyState) emptyState.style.display = 'block';
            return;
        }
        
        // Hide empty state
        if (emptyState) emptyState.style.display = 'none';
        
        devices.forEach(device => {
            const deviceItem = document.createElement('div');
            deviceItem.className = 'device-item';
            deviceItem.innerHTML = `
                <div class="device-info">
                    <div class="device-name">${device.description || device.ssid}</div>
                    <div class="device-details-scan">
                        <span class="device-type">${device.deviceType || 'Unknown'}</span>
                        <span class="device-rssi">Signal: ${device.rssi || 'N/A'} dBm</span>
                        ${device.configured ? '<span class="configured-badge">Configured</span>' : ''}
                    </div>
                </div>
                <button class="connect-btn-scan" onclick="iotSystem.connectToDevice('${device.deviceId}')">
                    Configure
                </button>
            `;
            deviceList.appendChild(deviceItem);
        });
        
        // Update device count
        const deviceCount = document.getElementById('deviceCount');
        if (deviceCount) {
            deviceCount.textContent = `Perangkat Terdeteksi: ${devices.length}`;
        }
    }

    updateLogDataDisplay() {
        const tableBody = document.getElementById('data-table-body');
        if (!tableBody) return;
        
        if (this.logData.length === 0) {
            tableBody.innerHTML = `
                <tr class="empty-row">
                    <td colspan="5">
                        <div class="empty-message">No data available. Connect a device to start logging data.</div>
                    </td>
                </tr>
            `;
            return;
        }
        
        tableBody.innerHTML = this.logData.map(item => `
            <tr>
                <td>${new Date(parseInt(item.timestamp)).toLocaleString()}</td>
                <td>${item.deviceName}</td>
                <td>${item.type}</td>
                <td>${item.value}</td>
                <td><span class="status-badge status-${item.status}">${item.status}</span></td>
            </tr>
        `).join('');
    }

    updateDeviceCount() {
        const footerElement = document.querySelector('.footer');
        if (footerElement) {
            footerElement.textContent = `Jumlah Perangkat: ${this.devices.length}`;
        }
    }

    // ==================== DEVICE COMMANDS ====================

    async sendCommand(deviceId, command) {
        try {
            const response = await fetch('/api/commands', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                    'Authorization': this.token
                },
                body: JSON.stringify({
                    deviceId: deviceId,
                    command: command,
                    parameters: {}
                })
            });

            if (response.ok) {
                this.showNotification(`Command ${command.toUpperCase()} sent successfully`, 'success');
            } else {
                this.showNotification('Failed to send command', 'error');
            }
        } catch (error) {
            console.error('Error sending command:', error);
            this.showNotification('Error sending command', 'error');
        }
    }

    // ==================== EVENT HANDLERS ====================
    
    setupEventListeners() {
        // Login form
        const loginForm = document.getElementById('loginForm');
        if (loginForm) {
            loginForm.addEventListener('submit', async (e) => {
                e.preventDefault();
                const username = document.getElementById('username').value;
                const password = document.getElementById('password').value;
                await this.login(username, password);
            });
        }

        // Config form
        const configForm = document.getElementById('configForm');
        if (configForm) {
            configForm.addEventListener('submit', async (e) => {
                e.preventDefault();
                
                // Check if we have a pending device from scan
                const pendingDeviceStr = localStorage.getItem('pendingDevice');
                let pendingDevice = null;
                
                if (pendingDeviceStr) {
                    try {
                        pendingDevice = JSON.parse(pendingDeviceStr);
                    } catch (e) {
                        console.error('Error parsing pending device:', e);
                    }
                }
                
                const config = {
                    deviceName: document.getElementById('deviceName').value,
                    readInterval: parseInt(document.getElementById('readInterval').value),
                    ipAddress: document.getElementById('ipAddress').value,
                    deviceType: document.querySelector('input[name="deviceType"]:checked').value,
                    deviceId: pendingDevice ? pendingDevice.deviceId : null
                };
                
                // Validation
                if (!config.deviceName.trim()) {
                    this.showNotification('Please enter a device name', 'error');
                    return;
                }
                
                if (!config.readInterval || config.readInterval < 1) {
                    this.showNotification('Please enter a valid read interval', 'error');
                    return;
                }
                
                if (!pendingDevice && !config.ipAddress.trim()) {
                    this.showNotification('Please enter an IP address', 'error');
                    return;
                }
                
                // IP validation only if manually entering
                if (!pendingDevice && config.ipAddress) {
                    const ipPattern = /^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
                    if (!ipPattern.test(config.ipAddress)) {
                        this.showNotification('Please enter a valid IP address format', 'error');
                        return;
                    }
                }
                
                await this.saveDeviceConfig(config);
                
                // Clear pending device
                localStorage.removeItem('pendingDevice');
            });
            
            // Pre-fill form if coming from scan
            const pendingDeviceStr = localStorage.getItem('pendingDevice');
            if (pendingDeviceStr) {
                try {
                    const pendingDevice = JSON.parse(pendingDeviceStr);
                    const deviceNameInput = document.getElementById('deviceName');
                    const ipInput = document.getElementById('ipAddress');
                    
                    if (deviceNameInput) {
                        deviceNameInput.value = pendingDevice.description || '';
                    }
                    
                    if (ipInput) {
                        ipInput.value = 'Auto-configured';
                        ipInput.disabled = true;
                    }
                    
                    // Set device type based on discovered type
                    if (pendingDevice.deviceType === 'sensor') {
                        const sensorRadio = document.querySelector('input[name="deviceType"][value="sensor"]');
                        if (sensorRadio) sensorRadio.checked = true;
                    } else if (pendingDevice.deviceType === 'actuator') {
                        const actuatorRadio = document.querySelector('input[name="deviceType"][value="actuator"]');
                        if (actuatorRadio) actuatorRadio.checked = true;
                    }
                } catch (e) {
                    console.error('Error loading pending device:', e);
                }
            }
        }

        // IP address formatting
        const ipInput = document.getElementById('ipAddress');
        if (ipInput && !ipInput.disabled) {
            ipInput.addEventListener('input', (e) => {
                let value = e.target.value;
                value = value.replace(/[^0-9.]/g, '');
                value = value.replace(/\.{2,}/g, '.');
                if (value.startsWith('.')) {
                    value = value.substring(1);
                }
                e.target.value = value;
            });
        }

        // Auto-focus next field
        document.querySelectorAll('.form-input').forEach((input, index, inputs) => {
            input.addEventListener('keypress', (e) => {
                if (e.key === 'Enter' && index < inputs.length - 1) {
                    e.preventDefault();
                    inputs[index + 1].focus();
                }
            });
        });
    }

    // ==================== INTERACTIVE METHODS ====================
    
    switchTab(tabName) {
        // Update tab appearance
        document.querySelectorAll('.tab').forEach(tab => {
            tab.classList.remove('active');
        });
        event.target.classList.add('active');

        // Show/hide device lists
        if (tabName === 'sensor') {
            const deviceList = document.getElementById('deviceList');
            const aktuatorList = document.getElementById('aktuatorList');
            if (deviceList) deviceList.style.display = 'block';
            if (aktuatorList) aktuatorList.style.display = 'none';
        } else {
            const deviceList = document.getElementById('deviceList');
            const aktuatorList = document.getElementById('aktuatorList');
            if (deviceList) deviceList.style.display = 'none';
            if (aktuatorList) aktuatorList.style.display = 'block';
        }
    }

    async toggleConnection(button, deviceId) {
        const statusText = button.parentElement.querySelector('.status-text');
        const isConnected = button.classList.contains('disconnect-btn');
        
        button.disabled = true;
        button.textContent = isConnected ? 'Disconnecting...' : 'Connecting...';
        
        try {
            await this.updateDeviceStatus(deviceId, isConnected ? 'disconnected' : 'connected');
            
            if (isConnected) {
                button.textContent = 'connect';
                button.classList.remove('disconnect-btn');
                button.classList.add('connect-btn');
                statusText.textContent = 'disconnected';
                statusText.classList.remove('status-connected');
                statusText.classList.add('status-disconnected');
            } else {
                button.textContent = 'disconnect';
                button.classList.remove('connect-btn');
                button.classList.add('disconnect-btn');
                statusText.textContent = 'connected';
                statusText.classList.remove('status-disconnected');
                statusText.classList.add('status-connected');
            }
        } catch (error) {
            console.error('Error toggling connection:', error);
            this.showNotification('Connection toggle failed', 'error');
        } finally {
            button.disabled = false;
        }
    }

    toggleExpand(icon) {
        const deviceDetails = icon.closest('.device-item-home').querySelector('.device-details');
        
        if (deviceDetails.classList.contains('show')) {
            deviceDetails.classList.remove('show');
            icon.classList.remove('expanded');
        } else {
            deviceDetails.classList.add('show');
            icon.classList.add('expanded');
        }
    }

    // ==================== UTILITY METHODS ====================
    
    showNotification(message, type = 'info') {
        const notification = document.createElement('div');
        notification.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: ${type === 'success' ? '#4CAF50' : type === 'error' ? '#f44336' : '#4285f4'};
            color: white;
            padding: 15px 20px;
            border-radius: 6px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.2);
            z-index: 1000;
            font-weight: 500;
            max-width: 300px;
            font-size: 14px;
        `;
        notification.textContent = message;
        
        document.body.appendChild(notification);
        
        setTimeout(() => {
            notification.remove();
        }, 3000);
    }

    startAutoRefresh() {
        // Auto-refresh device list and log data every 30 seconds
        this.refreshInterval = setInterval(() => {
            if (this.checkAuthentication()) {
                this.loadDevices();
                if (window.location.pathname.includes('logdata.html')) {
                    this.loadLogData();
                }
            }
        }, 30000);
    }

    stopAutoRefresh() {
        if (this.refreshInterval) {
            clearInterval(this.refreshInterval);
            this.refreshInterval = null;
        }
    }
}

// Global instance
let iotSystem;

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    iotSystem = new ESP32IoTSystem();
    
    // Load initial data based on current page
    const currentPage = window.location.pathname;
    
    if (currentPage.includes('home.html')) {
        iotSystem.loadDevices();
    } else if (currentPage.includes('logdata.html')) {
        iotSystem.loadLogData();
    }
});

// Global functions for backward compatibility
function switchTab(tabName) {
    if (iotSystem) iotSystem.switchTab(tabName);
}

function toggleConnection(button, deviceId) {
    if (iotSystem) iotSystem.toggleConnection(button, deviceId);
}

function toggleExpand(icon) {
    if (iotSystem) iotSystem.toggleExpand(icon);
}

function toggleAction(button, action, deviceId) {
    if (iotSystem) iotSystem.sendCommand(deviceId, action);
}

function toggleScan() {
    if (iotSystem) iotSystem.startDeviceScan();
}

function connectToDevice(deviceId) {
    if (iotSystem) iotSystem.connectToDevice(deviceId);
}

function scanDevices() {
    if (iotSystem) iotSystem.startDeviceScan();
}

// Handle page unload
window.addEventListener('beforeunload', () => {
    if (iotSystem) {
        iotSystem.stopAutoRefresh();
    }
});
