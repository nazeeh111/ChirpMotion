% Offline checks; run from repository root.
root = fileparts(fileparts(mfilename('fullpath')));
addpath(root);
fixture=[-32768;-100;-1;0;1;100;32767];
filename=[tempname '.pcm'];
cleanup=onCleanup(@() delete(filename));
fid=fopen(filename,'w'); assert(fid>=0); fwrite(fid,fixture,'int16'); fclose(fid);
actual=chirp_motion(filename);
assert(isequal(actual,fixture)); assert(isequal(actual,pcmread(filename)));
captures=dir(fullfile(root,'**','*.pcm'));
for k=1:numel(captures)
    data=chirp_motion(fullfile(captures(k).folder,captures(k).name));
    assert(numel(data)==floor(captures(k).bytes/2));
end
fprintf('PASS ChirpMotion: exact int16 boundary values and %d bundled PCM reads\n',numel(captures));
