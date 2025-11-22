
**11/21/2025**
Need to do a basic proof-of-concept for the whole acoustic triangulation concept. 

Ill get 3+ ultrasonic tweeters (probably more like 5 or 6 for testing interference) and an ultrasonic mic. 

I will record chirps at measured distances from each emitter, then use simulation software (matlab or a signal-processing python lib) to do filtering and signal processing. 

BTW: the lowpass filter for the ADC just has to be half or less than half of the ADC sample rate. Thus the sample rate will be the Nyquist rate. 


# Specifications
The transmitters will either need to be synchronized or encode the timestamp. 

FALSE: They can each send a radio packet as they're emitting a chirp, and the receiver can measure the delta-T between the radio packet and the sound packet. The delta-T will be basically just the time-of-flight of the sound wave. Probably the best approach as this requires absolutely zero synchronization between the beacons. But technically, each beacon should be chirping within a few hundred milliseconds of one another to get pinpoint accuracy (in case pet moves between chirps spaced far apart).

These are all considerations to factor in after a simple PoC. So let's find some hardware to use for measurements.



# Miscellaneous Considerations
The floor plan will actually need to be accounted for when there is no "line of sight" between a beacon and the receiver. In these cases, the time-of-flight will be trigonometric at best (one sharp reflection, like around a corner) or very, very complex at worst (many rounded reflections, like down a spiral staircase)





# Hardware plan
Im going to use my existing micro stock unless I absolutely need another chip.

Let's first figure out what cats and dogs hearing ranges are, and go about 5KHz above that as our transmission frequency.

Supposedly cats hear up to 85KHz, and dogs hear up to 60KHz.

Fuck it. Let's just round up to 95KHz or 100 KHz.



Alright. So we need 95-100KHz tweeters and mic. 
What am I doing for chirp and filter? Use actual shaped chirps or just fixed-frequency tones?

I basically want the most general-purpose transmitters for the prototyping phase to test different ideas quickly.



WAIT WAIT WAIT WAIT... I dont even need hardware yet. I can do this purely in simulation until I get the algorithms right. For a "hello world" PoC, just simulate three unique chirps being sent from different fixed distances, and toy around with the decoding algorithm until it can distinguish each chirp with all the interference. 


# Plan for now, 11-21-2025
The plan right now is to NOT use hardware at all yet; I will use Matlab (or python) to simulate the chirp transmit and receive, for various numbers of beacons and various noise levels and distributions.


Some considerations:
- the chirps must be sufficiently unique so that the receiver can distinguish them from one another
- alternatively, could figure out a coordination method so that each beacon waits till all echoes of the previous beacon's chirp have died out, so that there's no interference. NOT REALLY A VIABLE PLAN; the receiver will have to have filtering anyway. May as well design for simultaneous reception.


## Test plan
For a PoC, I will simulate 3 beacons at KNOWN DISTANCES (not locations, not yet; this phase is all about discriminating chirps from one another).

The matlab code will simulate as follows: Each beacon will use a unique chirp pattern. At t=0, each beacon will chirp. The speed of sound in conjunction with the distance of each beacon to receiver will be used to calculate reception time. Make sure beacons are ROUGHLY equidistant so that there is overlap between the received chirps, necessitating signal processing. The simulated receiver should simply discriminate the chirps and calculate time-of-flight of chirp since t=0, and from that calculate distance to each transmitter.

*Simulation is fucking sexy.*


