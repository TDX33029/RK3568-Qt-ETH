function capture = acm2108_udp_capture_demo(varargin)
% ACM2108 UDP capture helper for the vendor demo project.
%
% This function:
% 1. Sends the 4 control frames required by the FPGA design
% 2. Receives UDP payload bytes from the board
% 3. Decodes them into 16-bit samples
% 4. Plots either counter-test data or ADC waveform / FFT
%
% Quick start:
%   % Step 1: internal counter test
%   cap = acm2108_udp_capture_demo('channelSel', 0, 'sampleCount', 1024);
%
%   % Step 2: analog loopback, for example DA0 -> AD0
%   cap = acm2108_udp_capture_demo('channelSel', 1, 'sampleCount', 4096, ...
%       'divSet', 0, 'byteOrder', "big-endian");
%
% Notes:
% - PC IP should be configured as 192.168.0.3 by default
% - Board IP is 192.168.0.2 by default
% - Board command UDP port is 5000
% - Board sends captured data back to PC UDP port 6102

cfg = default_config();
cfg = apply_name_value_pairs(cfg, varargin{:});

fprintf('ACM2108 capture config:\n');
disp(cfg);

u = udpport("byte", "IPV4", ...
    "LocalPort", cfg.localPort, ...
    "Timeout", cfg.readTimeoutS);
cleanupObj = onCleanup(@() clear_udp(u)); %#ok<NASGU>

flush_pending_bytes(u);
pause(cfg.commandGapS);

send_cmd(u, cfg.boardIp, cfg.boardCmdPort, 1, cfg.channelSel);
pause(cfg.commandGapS);
send_cmd(u, cfg.boardIp, cfg.boardCmdPort, 2, cfg.sampleCount);
pause(cfg.commandGapS);
send_cmd(u, cfg.boardIp, cfg.boardCmdPort, 3, cfg.divSet);
pause(cfg.commandGapS);
send_cmd(u, cfg.boardIp, cfg.boardCmdPort, 0, 0);

expectedBytes = cfg.sampleCount * 2;
rawBytes = recv_exact_bytes(u, expectedBytes, cfg.captureTimeoutS);

[samples16, byteOrderUsed] = decode_samples(rawBytes, cfg);
sampleU8 = uint8(bitand(samples16, uint16(255)));

capture = struct();
capture.config = cfg;
capture.rawBytes = rawBytes;
capture.samples16 = samples16;
capture.sampleU8 = sampleU8;
capture.byteOrderUsed = byteOrderUsed;
capture.fsEffective = 50e6 / double(cfg.divSet + 1);
capture.timestamp = datestr(now, 'yyyy-mm-dd HH:MM:SS');

if cfg.channelSel == 0
    capture.signal = double(sampleU8);
    capture.counterStats = analyze_counter(sampleU8);
else
    capture.signal = double(sampleU8) - 128.0;
    capture.signalStats = analyze_signal(capture.signal, capture.fsEffective);
end

if cfg.plotEnabled
    plot_capture(capture);
end

if cfg.saveMat
    outDir = fileparts(mfilename('fullpath'));
    outName = sprintf('capture_%s_ch%d_n%d.mat', ...
        datestr(now, 'yyyymmdd_HHMMSS'), cfg.channelSel, cfg.sampleCount);
    outPath = fullfile(outDir, outName);
    save(outPath, 'capture');
    capture.savedMatPath = outPath;
    fprintf('Saved MAT file: %s\n', outPath);
end
end

function cfg = default_config()
cfg = struct();
cfg.boardIp = "192.168.0.2";
cfg.localPort = 6102;
cfg.boardCmdPort = 5000;
cfg.channelSel = 0;
cfg.sampleCount = 1024;
cfg.divSet = 0;
cfg.byteOrder = "auto";
cfg.commandGapS = 0.05;
cfg.readTimeoutS = 0.2;
cfg.captureTimeoutS = 5.0;
cfg.plotEnabled = true;
cfg.saveMat = true;
end

function cfg = apply_name_value_pairs(cfg, varargin)
if mod(numel(varargin), 2) ~= 0
    error('Name-value arguments must appear in pairs.');
end

for k = 1:2:numel(varargin)
    name = char(varargin{k});
    value = varargin{k + 1};
    if ~isfield(cfg, name)
        error('Unknown option: %s', name);
    end
    cfg.(name) = value;
end
end

function send_cmd(u, boardIp, boardPort, addr, value)
frame = pack_cmd_frame(addr, value);
frameText = char(upper(join(string(dec2hex(frame, 2)), " ")));
write(u, frame, "uint8", boardIp, boardPort);
fprintf('Sent cmd: addr=%d value=%u frame=%s\n', ...
    addr, uint32(value), frameText);
end

function frame = pack_cmd_frame(addr, value)
v = uint32(value);
frame = uint8([ ...
    hex2dec('55'), ...
    hex2dec('A5'), ...
    uint8(addr), ...
    uint8(bitand(bitshift(v, -24), 255)), ...
    uint8(bitand(bitshift(v, -16), 255)), ...
    uint8(bitand(bitshift(v, -8), 255)), ...
    uint8(bitand(v, 255)), ...
    hex2dec('F0') ...
]);
end

function raw = recv_exact_bytes(u, expectedBytes, timeoutS)
raw = zeros(1, expectedBytes, 'uint8');
filled = 0;
tic;

while filled < expectedBytes
    if toc > timeoutS
        error('Timed out while waiting for UDP data. Received %d / %d bytes.', ...
            filled, expectedBytes);
    end

    avail = u.NumBytesAvailable;
    if avail <= 0
        pause(0.01);
        continue;
    end

    chunk = read(u, avail, "uint8");
    take = min(numel(chunk), expectedBytes - filled);
    raw(filled + 1 : filled + take) = chunk(1:take);
    filled = filled + take;
end

fprintf('Received %d bytes from board.\n', filled);
end

function [samples16, byteOrderUsed] = decode_samples(rawBytes, cfg)
if mod(numel(rawBytes), 2) ~= 0
    rawBytes = rawBytes(1:end-1);
    warning('Odd byte count received. Last byte was discarded.');
end

pairs = reshape(rawBytes, 2, []);
samplesBE = uint16(pairs(1, :)) * 256 + uint16(pairs(2, :));
samplesLE = uint16(pairs(2, :)) * 256 + uint16(pairs(1, :));

byteOrder = string(cfg.byteOrder);
if byteOrder == "big-endian"
    samples16 = samplesBE;
    byteOrderUsed = "big-endian";
    return;
end

if byteOrder == "little-endian"
    samples16 = samplesLE;
    byteOrderUsed = "little-endian";
    return;
end

if cfg.channelSel == 0
    scoreBE = counter_score(uint8(bitand(samplesBE, uint16(255))));
    scoreLE = counter_score(uint8(bitand(samplesLE, uint16(255))));
    if scoreBE >= scoreLE
        samples16 = samplesBE;
        byteOrderUsed = "big-endian";
    else
        samples16 = samplesLE;
        byteOrderUsed = "little-endian";
    end
else
    samples16 = samplesBE;
    byteOrderUsed = "big-endian";
end
end

function score = counter_score(x)
if numel(x) < 2
    score = 0.0;
    return;
end
d = mod(double(x(2:end)) - double(x(1:end-1)), 256.0);
score = mean(d == 1.0);
end

function stats = analyze_counter(x)
x = uint8(x(:).');
d = mod(double(x(2:end)) - double(x(1:end-1)), 256.0);
expected = mod(double(x(1)) + (0:numel(x)-1), 256.0);

stats = struct();
stats.firstValue = double(x(1));
stats.lastValue = double(x(end));
stats.stepMatchRatio = mean(d == 1.0);
stats.fullMatchRatio = mean(double(x) == expected);
end

function stats = analyze_signal(signal, fs)
signal = double(signal(:));
signal = signal - mean(signal);

n = numel(signal);
nfft = 2^nextpow2(max(n, 256));
win = local_hann(n);
spectrum = fft(signal .* win, nfft);
mag = abs(spectrum(1:nfft/2+1));
freq = (0:nfft/2) * fs / nfft;

if numel(mag) >= 2
    [peakMag, idx] = max(mag(2:end));
    idx = idx + 1;
else
    peakMag = mag(1);
    idx = 1;
end

stats = struct();
stats.rms = sqrt(mean(signal .^ 2));
stats.peakToPeak = max(signal) - min(signal);
stats.freqAxis = freq;
stats.fftMag = mag;
stats.peakFreqHz = freq(idx);
stats.peakMag = peakMag;
end

function plot_capture(capture)
cfg = capture.config;
signal = capture.signal(:);
showN = min(numel(signal), 512);

figure('Name', 'ACM2108 Capture', 'Color', 'w');
tiledlayout(2, 1, 'Padding', 'compact', 'TileSpacing', 'compact');

nexttile;
plot(0:showN-1, signal(1:showN), 'LineWidth', 1.0);
grid on;
xlabel('Sample Index');
ylabel('Amplitude');
title(sprintf('Time Domain | ch=%d | N=%d | byteOrder=%s', ...
    cfg.channelSel, cfg.sampleCount, capture.byteOrderUsed));

nexttile;
if cfg.channelSel == 0
    stairs(0:showN-1, double(capture.sampleU8(1:showN)), 'LineWidth', 1.0);
    grid on;
    xlabel('Sample Index');
    ylabel('Counter Value');
    title(sprintf('Counter Test | fullMatch=%.4f | stepMatch=%.4f', ...
        capture.counterStats.fullMatchRatio, ...
        capture.counterStats.stepMatchRatio));
else
    plot(capture.signalStats.freqAxis / 1e6, capture.signalStats.fftMag, 'LineWidth', 1.0);
    grid on;
    xlabel('Frequency (MHz)');
    ylabel('|FFT|');
    title(sprintf('FFT | fs_{eff}=%.3f MHz | peak=%.6f MHz', ...
        capture.fsEffective / 1e6, ...
        capture.signalStats.peakFreqHz / 1e6));
end
end

function flush_pending_bytes(u)
while u.NumBytesAvailable > 0
    read(u, u.NumBytesAvailable, "uint8");
    pause(0.01);
end
end

function clear_udp(u)
if isempty(u)
    return;
end
flush_pending_bytes(u);
clear u;
end

function w = local_hann(n)
if n <= 1
    w = ones(n, 1);
    return;
end
k = (0:n-1).';
w = 0.5 - 0.5 * cos(2 * pi * k / (n - 1));
end
