function result = chirp_motion_analyze(input, opts)
%CHIRP_MOTION_ANALYZE Toolbox-free offline linear-FMCW spectral analysis.
% input: mono numeric samples, little-endian signed int16 PCM, or WAV file.
% RIFF/WAVE headers are recognized regardless of extension; inputChannel is
% required for stereo WAV. WAV samples use audioread normalized amplitudes.
% Default legacy waveform: 48 kHz, 17->23 kHz, 20 ms, 961 samples including
% both endpoints, symmetric Hann window. mode='alternating' uses alternating
% up/down sweeps from fmcw_dou.m; mode='up' uses fmcw.m's single sweep.
% startSample explicitly fixes the first sweep (1-based). If empty, locate
% the earliest near-best normalized match to a complete waveform period.
% Automatic sync removes the dominant arrival delay, so reported delays and
% path lengths are relative to that arrival, NOT calibrated absolute ranges.
% No playback, network, filtering toolbox, classifier or geometry solver.
if nargin<2, opts=struct; end
if ~isstruct(opts)||~isscalar(opts), error('ChirpMotion:Options','opts must be a scalar struct.'); end
defaults=struct('sampleRate',48000,'startFrequency',17000,'endFrequency',23000, ...
    'sweepSeconds',.02,'fftLength',16384,'maxBeatHz',1500,'soundSpeed',340, ...
    'mode','up','startSample',[],'inputChannel',[],'minAlignmentScore',.1);
unknown=setdiff(fieldnames(opts),fieldnames(defaults));
if ~isempty(unknown), error('ChirpMotion:Options','Unknown option: %s',unknown{1}); end
names=fieldnames(defaults);
for j=1:numel(names), if ~isfield(opts,names{j}), opts.(names{j})=defaults.(names{j}); end, end
for field={'sampleRate','startFrequency','endFrequency','sweepSeconds','maxBeatHz','soundSpeed'}
    validateattributes(opts.(field{1}),{'numeric'},{'real','finite','scalar','positive'});
end
validateattributes(opts.fftLength,{'numeric'},{'real','finite','scalar','integer','positive'});
if ~any(strcmp(opts.mode,{'up','alternating'})), error('ChirpMotion:Mode','mode must be up or alternating.'); end
if opts.startFrequency>=opts.endFrequency || opts.endFrequency>=opts.sampleRate/2 || opts.maxBeatHz>=opts.sampleRate/2
    error('ChirpMotion:Frequency','Require 0 < startFrequency < endFrequency < Nyquist and maxBeatHz < Nyquist.');
end
validateattributes(opts.minAlignmentScore,{'numeric'},{'real','finite','scalar','nonnegative','<=',1});
if opts.maxBeatHz >= min(2*opts.startFrequency,opts.sampleRate-2*opts.endFrequency)
    error('ChirpMotion:Aliasing','maxBeatHz must lie below the real-mixer sum-frequency alias band.');
end
if ischar(input)||(isstring(input)&&isscalar(input))
    fid=fopen(input,'rb','ieee-le');
    if fid<0, error('ChirpMotion:File','Cannot open PCM file.'); end
    cleanup=onCleanup(@() fclose(fid));
    header=fread(fid,12,'uint8=>char')'; fseek(fid,0,'bof');
    if numel(header)==12 && strcmp(header(1:4),'RIFF') && strcmp(header(9:12),'WAVE')
        [audio,fileRate]=audioread(input);
        if fileRate~=opts.sampleRate, error('ChirpMotion:SampleRate','WAV sample rate disagrees with options.'); end
        if size(audio,2)>1 && isempty(opts.inputChannel)
            error('ChirpMotion:Channel','Multichannel WAV requires inputChannel (gesture.pcm is a stereo WAV).');
        end
        channel=opts.inputChannel; if isempty(channel), channel=1; end
        validateattributes(channel,{'numeric'},{'scalar','integer','positive','<=',size(audio,2)});
        x=audio(:,channel);
    else
        if ~isempty(opts.inputChannel) && ~isequal(opts.inputChannel,1)
            error('ChirpMotion:Channel','Raw PCM is mono; inputChannel must be empty or 1.');
        end
        fseek(fid,0,'eof'); bytes=ftell(fid); fseek(fid,0,'bof');
        if mod(bytes,2), error('ChirpMotion:PCM','PCM byte count must be even.'); end
        x=fread(fid,Inf,'int16=>double');
    end
else
    x=input;
end
validateattributes(x,{'numeric'},{'real','finite','vector','nonempty'});
x=double(x(:));
N=round(opts.sampleRate*opts.sweepSeconds)+1;
if N<3 || opts.fftLength<N, error('ChirpMotion:Length','Need at least 3 sweep samples and fftLength >= sweep samples.'); end
% Reject a fractional sample interval rather than silently changing timing.
if abs((N-1)-opts.sampleRate*opts.sweepSeconds)>1e-8
    error('ChirpMotion:Timing','sampleRate * sweepSeconds must be an integer.');
end
t=(0:N-1)'/opts.sampleRate; window=.5-.5*cos(2*pi*(0:N-1)'/(N-1));
slope=(opts.endFrequency-opts.startFrequency)/opts.sweepSeconds;
model=cos(2*pi*(opts.startFrequency*t+.5*slope*t.^2)).*window;
if strcmp(opts.mode,'alternating')
    model(:,2)=cos(2*pi*(opts.endFrequency*t-.5*slope*t.^2)).*window;
end
period=model(:); P=numel(period);
if numel(x)<P, error('ChirpMotion:Short','Input must contain at least one complete waveform period.'); end
score=NaN;
if isempty(opts.startSample)
    % Linear correlation via convolution; valid lags cannot wrap the input.
    L=2^nextpow2(numel(x)+P-1);
    correlation=real(ifft(fft(x,L).*fft(flipud(period),L)));
    correlation=correlation(P:numel(x));
    energy=[0;cumsum(x.^2)]; energy=energy(P+1:end)-energy(1:end-P);
    normFactor=sqrt(max(energy,0)*sum(period.^2));
    valid=normFactor>0;
    normalized=zeros(size(correlation)); normalized(valid)=abs(correlation(valid))./normFactor(valid);
    score=max(normalized);
    if score<=0 || score<opts.minAlignmentScore
        error('ChirpMotion:NoSignal','Waveform match %.4g is below minAlignmentScore; check format, mode and timing.',score);
    end
    first=find(normalized>=.99*score,1,'first');
else
    validateattributes(opts.startSample,{'numeric'},{'real','finite','scalar','integer','positive'});
    first=opts.startSample;
end
frames=floor((numel(x)-first+1)/P);
if frames<1, error('ChirpMotion:Short','No complete waveform period remains after startSample.'); end
last=first+frames*P-1;
chunks=reshape(x(first:last),N,[]);
frequency=(0:floor(opts.fftLength/2))'*opts.sampleRate/opts.fftLength;
keep=frequency<=opts.maxBeatHz; frequency=frequency(keep);
channels=size(model,2); spectra=zeros(numel(frequency),frames,channels);
peakHz=NaN(frames,channels); activity=zeros(frames,channels);
for c=1:channels
    mixed=chunks(:,c:channels:end).*model(:,c);
    spectrum=abs(fft(mixed,opts.fftLength,1));
    spectrum=spectrum(1:numel(frequency),:);
    spectra(:,:,c)=spectrum;
    [amplitude,idx]=max(spectrum,[],1);
    peaks=frequency(idx); peaks(amplitude==0)=NaN;
    peakHz(:,c)=peaks(:);
    % Relative L2 spectral change; first frame has no predecessor.
    activity(1,c)=NaN;
    if frames>1
        denom=sqrt(sum(spectrum(:,1:end-1).^2,1));
        delta=sqrt(sum(diff(spectrum,1,2).^2,1));
        ratio=delta./denom; ratio(denom==0)=NaN;
        activity(2:end,c)=ratio(:);
    end
end
result.frequencyHz=frequency;
result.spectra=spectra;
result.peakBeatHz=peakHz;
result.delaySeconds=peakHz/slope;
result.pathLengthMeters=result.delaySeconds*opts.soundSpeed;
result.frameTimeSeconds=((first-1)+(0:frames-1)'*P)/opts.sampleRate;
result.channelOffsetSeconds=(0:channels-1)*N/opts.sampleRate;
result.relativeSpectralChange=activity;
result.startSample=first;
result.alignmentScore=score;
result.discardedPrefixSamples=first-1;
result.discardedSuffixSamples=numel(x)-last;
result.options=opts;
result.binSpacingHz=opts.sampleRate/opts.fftLength;
result.nominalPathResolutionMeters=opts.soundSpeed/(opts.endFrequency-opts.startFrequency);
end
