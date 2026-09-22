function varargout = chirp_motion(varargin)
% ChirpMotion: Read motion through acoustic chirps.
% Passes arguments and outputs directly to pcmread.
root = fileparts(mfilename('fullpath'));
previousPath = path;
cleanup = onCleanup(@() path(previousPath)); %#ok<NASGU>
addpath(root);
[varargout{1:nargout}] = pcmread(varargin{:});
end
