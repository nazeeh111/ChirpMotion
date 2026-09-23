% Offline tests using independent delayed chirps, then all bundled captures.
root=fileparts(fileparts(mfilename('fullpath'))); addpath(root);
fs=48000; T=.02; slope=6000/T; N=961; t=(0:N-1)'/fs;
w=.5-.5*cos(2*pi*(0:N-1)'/(N-1));
% Analytic delayed carrier produces beat frequency slope*tau; no sync removal.
tau=.001; expectedHz=slope*tau;
received=cos(2*pi*(17000*(t-tau)+.5*slope*(t-tau).^2)).*w;
r=chirp_motion_analyze(repmat(received,8,1),struct('startSample',1));
frequencyError=max(abs(r.peakBeatHz-expectedHz));
assert(frequencyError<=r.binSpacingHz);
assert(max(abs(r.pathLengthMeters-.34))<=r.binSpacingHz/slope*340);
assert(all(r.relativeSpectralChange(2:end)==0));
assert(size(r.spectra,2)==8 && r.startSample==1);
% Alternate up/down carriers use their own correctly oriented templates.
down=cos(2*pi*(23000*(t-tau)-.5*slope*(t-tau).^2)).*w;
r=chirp_motion_analyze(repmat([received;down],6,1),struct('mode','alternating','startSample',1));
assert(all(abs(r.peakBeatHz-expectedHz)<=r.binSpacingHz,'all'));
% Leading silence recovered without the original script's extra +1 offset.
model=cos(2*pi*(17000*t+.5*slope*t.^2)).*w;
r=chirp_motion_analyze([zeros(137,1);repmat(model,5,1);zeros(19,1)]);
assert(r.startSample==138 && size(r.spectra,2)==5 && r.discardedSuffixSamples==19);
assert(r.alignmentScore>.999);
for x={zeros(1000,1),ones(100,1),[1 NaN],[]}
    failed=false; try, chirp_motion_analyze(x{1}); catch, failed=true; end; assert(failed);
end
files=dir(fullfile(root,'*.pcm'));
for j=1:numel(files)
    % e-series use the alternating legacy model; gesture/Eye use up-sweeps.
    mode='up'; if ~isempty(regexp(files(j).name,'^e[0-9]+\.pcm$','once')), mode='alternating'; end
    options=struct('mode',mode);
    if strcmp(files(j).name,'gesture.pcm'), options.inputChannel=2; end
    r=chirp_motion_analyze(fullfile(root,files(j).name),options);
    assert(all(isfinite(r.spectra),'all') && any(r.spectra>0,'all'));
    assert(size(r.spectra,2)>=1 && all(r.peakBeatHz>=0,'all'));
    fprintf('CAPTURE %s: %d frames, %d channels, alignment %.4f, start %d\n',files(j).name,size(r.spectra,2),size(r.spectra,3),r.alignmentScore,r.startSample);
end
failed=false; try, chirp_motion_analyze(fullfile(root,'gesture.pcm')); catch err, failed=strcmp(err.identifier,'ChirpMotion:Channel'); end; assert(failed);
fprintf('PASS ChirpMotion offline: 300 Hz echo peak error %.6g Hz; %d bundled captures\n',frequencyError,numel(files));
