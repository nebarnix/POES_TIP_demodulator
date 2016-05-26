# Project Desert Tortoise - Demodulator
## And a tiny app to decode the ground transmitters as well
A demodulator written in C to decode IQ data from the 137.35Mhz/137.77Mhz telemetry beacons from NOAA-15, NOAA-18, and NOAA-19

See the background of this project here
http://wiki.nebarnix.com/wiki/NOAA_POES_TIP_Demodulation

Attribution:
DSP code for the M&M Clock Recovery and Carrier Tracking PLL adapted from GNURADIO source.
Wave file format parser adapted from http://truelogic.org/wordpress/2015/09/04/parsing-a-wav-file-in-c/ 

Todo in decreasing order of importance:
- [X] Add dynamic lowpass filter coefficient generator to compensate for varying input sample rates
- [X] Add sync word detection for metrics
- [X] Add sync word byte conversion to form minor frame data. 
- [ ] Add in 8x interpolation for the M&M clock recovery routine for better performance. 
- [X] Keep track of local recording time and process it in parallel with the data. We will need this through ALL steps of processing
- [X] Add in ability to enable/disable/override things through command line options 
- [ ] Add support for RAW data files
- [ ] Add soundcard input capability 
- [ ] Add the rest of the processing chain
- [ ] Add a gui to make pretty things happen in realtime
- [ ] Add hilbert transform to allow for real data input (large bandwidth USB or LSB recording)

# ARGOS demodulator
- Demodulates 401.65Mhz transmissions from ground transmitters to POES satellites
- Spits out packets to packets.txt. 
- Takes in wave files. 
- Mostly works! (NEW!)
