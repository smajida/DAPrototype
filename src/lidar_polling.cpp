#include <iostream>
#include <atomic>
#include <thread>
#include "pace_setter_class.h"
#include "process_values_class.h"
#include "fcw_tracker_class.h"
#include "xml_reader.h"

#define FEETPERCENTIMETER 0.0328084

#ifndef __arm__									//Detect if not compiling for raspberry pi
	void LidarPolingThread( ProcessValues *processvalues,
							std::atomic<bool> *exitsignal )
	{

		std::cout << "Lidar polling thread starting!" << '\n';
		processvalues->fcwstatus_ = 0;
		std::cout << "Lidar not operational, exiting thread!" << '\n';
		return;
	}
#endif

#ifdef __arm__									//Detect if compiling for raspberry pi
	#include <wiringPi.h>
	#include <wiringPiI2C.h>
	
	extern "C" {
		#include "lidarLite.h"
	}

	void LidarPolingThread( ProcessValues *processvalues,
							std::atomic<bool> *exitsignal )
	{

		std::cout << "Lidar polling thread starting!" << '\n';

		//Create thread variables
		double followingdistance{0.0};
		bool vehiclemoving{false};
		int pullaheaddelay{ settings::comm::kpollratelidar / 2 };	//500ms
		int pullaheadcount{ 0 };
		FcwTracker fcwtracker( settings::fcw::kdistanceoffset,
			settings::fcw::ksamplestoaverage );

		//Setup I2C
		//wiringPiSetupGpio();
		int dacModule = wiringPiI2CSetup(0x62);
		if (dacModule < 0)
		{
			std::cout << "I2C Setup Error" << '\n';
			return;
		}

		//create pace setter
		PaceSetter lidarpacer(settings::comm::kpollratelidar, "lidar polling");

		//Loop indefinitely
		while( !(*exitsignal) ) {

			//Read FCW distance
			int fcwresult{ -1 };
			bool readerror{ true };
			fcwresult = lidar_read(dacModule);
			if ( fcwresult > 0 ) {
				followingdistance = FEETPERCENTIMETER * fcwresult;
				readerror = false;
			}
			
			fcwtracker.Update( followingdistance, processvalues->gpsspeed_);
			
			//Check if vehicle is moving
			if ( processvalues->gpsspeed_ > 1.0 ) {
				vehiclemoving = true;

			} else {
				vehiclemoving = false;
			}

			//Update everything
			processvalues->forwarddistance_ = fcwtracker.followingdistance_;
			if ( fcwtracker.followingtime_ < fcwtracker.timetocollision_ ) {
				processvalues->timetocollision_ = fcwtracker.followingtime_;
			} else {
				processvalues->timetocollision_ = fcwtracker.timetocollision_;			
			}

			//FCW Status
			//-1 = error (sensor error)
			//0 = inactive (disabled by xml or zero speed)
			//1 = fcw warning
			//2 = following too close warning
			//3 = fcw alarm
			//4 = following too close alarm
			//5 = driver ahead takeoff notification
			if (readerror) {
				processvalues->fcwstatus_ = -1;
				processvalues->fcwpwmvalue_ = 0;
			} else if ( (1000*fcwtracker.timetocollision_) < settings::fcw::kmscollisionwarning ) {
				processvalues->fcwstatus_ = 1;
				processvalues->fcwpwmvalue_ = 1023 + static_cast<int>((1024.0*(1000*
					fcwtracker.timetocollision_ - settings::fcw::kmscollisionalarm)) /
					(settings::fcw::kmscollisionalarm - settings::fcw::kmscollisionwarning));
			} else if ( vehiclemoving && (1000*fcwtracker.followingtime_) <
				settings::fcw::kmsfollowdistwarning ) {
				processvalues->fcwstatus_ = 2;
				processvalues->fcwpwmvalue_ = 1023 + static_cast<int>((1024.0*(1000*
					fcwtracker.followingtime_ - settings::fcw::kmsfollowdistalarm)) /
					(settings::fcw::kmsfollowdistalarm - settings::fcw::kmsfollowdistwarning));
			} else if ( (1000*fcwtracker.timetocollision_) < settings::fcw::kmscollisionalarm ) {
				processvalues->fcwstatus_ = 3;
				processvalues->fcwpwmvalue_ = 1023;
			} else if ( vehiclemoving && (1000*fcwtracker.followingtime_) <
				settings::fcw::kmsfollowdistalarm ){
				processvalues->fcwstatus_ = 4;
				processvalues->fcwpwmvalue_ = 1023;
			} else {
				processvalues->fcwstatus_ = 0;
				processvalues->fcwpwmvalue_ = 0;
			}
			if (processvalues->fcwpwmvalue_ < 0) {
				processvalues->fcwpwmvalue_ = 0;
			} else if (processvalues->fcwpwmvalue_ > 1023) {
				processvalues->fcwpwmvalue_ = 1023;
			}
			if ( !vehiclemoving || (fcwtracker.timetocollision_ < fcwtracker.followingtime_) &&
				(processvalues->fcwstatus_ == 0) ) {
				processvalues->fcwpwmvalue_ = 1023 + static_cast<int>((1024.0*(1000*
					fcwtracker.timetocollision_ - settings::fcw::kmscollisionwarning)) /
					(settings::fcw::kmscollisionwarning));			
			} else if ( vehiclemoving && (fcwtracker.timetocollision_ >
				fcwtracker.followingtime_) && (processvalues->fcwstatus_ == 0) ) {
				processvalues->fcwpwmvalue_ = 1023 + static_cast<int>((1024.0*(1000*
					fcwtracker.followingtime_ - settings::fcw::kmsfollowdistwarning)) /
					(settings::fcw::kmsfollowdistwarning));			
			}

			//Check for driver pullahead
			if ( !vehiclemoving && (fcwtracker.acceleration_ > 0.1) && (
				processvalues->fcwstatus_ = 0) && (pullaheadcount > pullaheaddelay) ) {
				processvalues->fcwstatus_ = 5;
			} else if ( !vehiclemoving && (fcwtracker.acceleration_ > 0.1) && (
				processvalues->fcwstatus_ = 0) ) {
				pullaheadcount++;
			} else {
				pullaheadcount = 0;
			}

			//Setpace
			lidarpacer.SetPace();
		}

		std::cout << "Exiting Lidar polling thread!" << '\n';
		return;

	}
#endif


