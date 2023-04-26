clc;clear;
fs = 5000;
[b_stop,a_stop] = butter(2,[45 55]/(fs/2),'stop');
% [b_pass,a_pass] = butter(2,[1 4]/(fs/2),'bandpass');
[b_pass,a_pass] = butter(2,[0.2 30]/(fs/2),'bandpass');
% [b_pass,a_pass] = butter(2,[8 12]/(fs/2),'bandpass');
fileID=fopen('filter_coef.bin','w');

fwrite(fileID,size(a_stop,2),'int32');
for k=1:length(a_stop)
    fwrite(fileID,a_stop(k),'double');
end
for k=1:length(b_stop)
    fwrite(fileID,b_stop(k),'double');
end
for k=1:length(a_pass)
    fwrite(fileID,a_pass(k),'double');
end
for k=1:length(b_pass)
    fwrite(fileID,b_pass(k),'double');
end
fclose(fileID);
