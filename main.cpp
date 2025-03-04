#include <iostream>
#include <fstream>
#include <iomanip>		// RAD: so far used only for setprecission() in cmd output
#include <time.h>
#include <vector>
#include "functions.h"

using namespace std;

/* Additions/changes to REGCAP (WJNT)
Output files = .rco (minute-by-minute) and .rc2 (annual summary)
Input files = .csv
Occupancy scheduling including calculation of occupied dose and exposure
Dynamic exhuast fan scheduling - reads in new input files
Economizer overides thermostat control to prevent heating after pre-cooling (unless you use the 7-day running average
swtich between heating and cooling operation - then the economizer only runs on cooling days i.e. hcFlag = 2)
Economizer operation opens pressure relief
Aeq calculation options for no infiltration, default 62.2 infiltration credit, or Addendum N weather factors
Terrain wind and shelter coefficients are selectable
Running average thermostat
Filter loading effects (new subroutine) - initial performance and loading performance impacts
Passive stacks/flues can be flow limited to the mechanical 62.2 rate

RIVEC:
	Exposure limit equation added
	New RIVEC control agorithm (fan.oper = 50)
	RIVEC algorithm can include infiltration credits
	Controller can close passive stacks (Hybrid applications)

New inputs:
	Bathroom scheduling flag (now redundant in C++)
	Furnace AFUE efficiency (AFUE)
	Number of bedrooms (numBedrooms)
	Number of stories (numStories)
	Addendum N weather factors (weatherFactor)
*/

// Main function
int main(int argc, char *argv[])
{ 	
	// Simulation Batch Timing
	time_t startTime, endTime;
	time(&startTime);
	string runStartTime = ctime(&startTime);
	
	// [File Paths] Paths for input and output file locations
	string inPath = "Z:\\humidity_bdless\\newRHfix\\50%FlowRateFix\\";				// Location of input files. Brennan. 
	string outPath = "Z:\\humidity_bdless\\out_RHfix\\50%FlowRateFix\\";				// Location to write output files. Brennan.
	
	string batchFile_name = inPath + "bat_50cfis_AllCases_b.txt";	// Name of batch input file (assumed to be in the same folder as the input files). Brennan. Originally .csv file. 
	
	string weatherPath = "C:\\RC++\\weather\\IECC\\";			// Location of IECC weather files (DOE, All of US). \\ "adjusted\\" contains files w/median values appended
	//string weatherPath = "C:\\RC++\\weather\\CEC\\";			// Location of CEC weather files (California)
	
	string shelterFile_name = "C:\\RC++\\shelter\\bshelter.dat";	// Location of shelter file

	// Set to a, b or c to avoid potential conflicts while running more than one simulation
	// at the same time using a common dynamic fan schedule input file
	string SCHEDNUM = "a";
	string fanSchedulefile_name1 = "C:\\RC++\\schedules\\sched1" + SCHEDNUM;
	string fanSchedulefile_name2 = "C:\\RC++\\schedules\\sched2" + SCHEDNUM;
	string fanSchedulefile_name3 = "C:\\RC++\\schedules\\sched3" + SCHEDNUM;
	
	// total days to run the simulation for:
	int totaldays = 365;

	char reading[255];
	int numSims;			// Number of simulations to run in the batch
	string simFile[255], climateZone[255], outName[255];

	cout << "\nRC++ [begin]" << endl << endl;

	// [start] Reading Batch File ============================================================================================
	ifstream batchFile(batchFile_name); 
	if(!batchFile) { 
		cout << "Cannot open: " << batchFile_name << endl;
		system("pause");
		return 1; 
	} 

	batchFile.getline(reading, 255);
	numSims = atoi(reading);

	cout << "Batch file info:" << endl;
	for(int i=0; i < numSims; i++) {
		getline(batchFile, simFile[i]);
		getline(batchFile, climateZone[i]);
		getline(batchFile, outName[i]);

		cout << "simFile[" << i << "]: " << simFile[i] << endl;
		cout << "climateZone[" << i << "]: " << climateZone[i] << endl;
		cout << "outName[" << i << "]: " << outName[i] << endl << endl;		
	}

	batchFile.close();
	// [END] Reading Batch File ============================================================================================

	for(int sim=0; sim < numSims; sim++) {		

		// Declare structures
		winDoor_struct winDoor[10] = {0};
		fan_struct fan[10] = {0};
		fan_struct atticFan[10] = {0};
		pipe_struct Pipe[10] = {0};		
		atticVent_struct atticVent[10] = {0};
		soffit_struct soffit[4] = {0};
		flue_struct flue[6] = {0};

		//Declare arrays
		double Sw[4];
		double floorFraction[4];
		double wallFraction[4];
		double Swinit[4][361];		
		double mFloor[4] = {0,0,0,0};
		double soffitFraction[5];
		double wallCp[4] = {0,0,0,0};
		double mechVentPower;
		double b[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
		double tempOld[16];
		double hrold[5];
		double HR[5];		
		double heatThermostat[24];
		double coolThermostat[24];

		// Zeroing the variables to create the sums for the .ou2 file
		long int MINUTE = 1;
		int endrunon = 0;
		int prerunon = 0;
		//int timeSteps = 0;
		int bsize = 0;
		double Mcoil = 0;
		double SHR = 0;		
		double meanOutsideTemp = 0;
		double meanAtticTemp = 0;
		double meanHouseTemp = 0;
		double meanHouseACH = 0;
		double meanFlueACH = 0;
		double gasTherm = 0;
		double AH_kWh = 0;
		double compressor_kWh = 0;
		double mechVent_kWh = 0;
		double furnace_kWh = 0;
		double C1 = -5.6745359E+03; //Coefficients for saturation vapor pressure over liquid water -100 to 0C. ASHRAE HoF.
		double C2 = 6.3925247E+00;
		double C3 = -9.6778430E-03;
		double C4 = 6.2215701E-07;
		double C5 = 2.0747825E-09;
		double C6 = -9.4840240E-13;			
		double C7 = 4.1635019E+00;
		double C8 = -5.8002206E+03; //Coefficients for saturation vapor pressure over liquid water 0 to 200C. ASHRAE HoF.
		double C9 = 1.3914993E+00;
		double C10 = -4.8640239E-02;
		double C11 = 4.1764768E-05;
		double C12 = -1.4452093E-08;
		double C13 = 6.5459673E+00;
		double SatVaporPressure = 0; //Saturation vapor pressure, pa
		double HRsaturation = 0; //humidity ratio at saturation
		double RHhouse = 50; //house relative humidity
		double RHind60 = 0; //index value (0 or 1) if RHhouse > 60 
		double RHtot60 = 0; //cumulative sum of index value (0 or 1) if RHhouse > 60 
		double RHexcAnnual60 = 0; //annual fraction of the year where RHhouse > 60
		double RHind70 = 0; //index value (0 or 1) if RHhouse > 70 
		double RHtot70 = 0; //cumulative sum of index value (0 or 1) if RHhouse > 70
		double RHexcAnnual70 = 0; //annual fraction of the year where RHhouse > 70

		// "constants":
		double airDensityRef = 1.20411;	// Reference air density at 20 deg C at sea level [kg/m3]
		double airTempRef = 293.15;		// Reference room temp [K] = 20 deg C
		double pi = 3.1415926;

		double hret = 28;				// Initial number for hret in Btu/lb

		// [START] Thermostat settings (RIVEC 2012) ========================================================================================

		// Heating. Building America Simulation Protocols
		heatThermostat[0] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[1] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[2] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[3] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[4] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[5] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[6] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[7] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[8] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[9] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[10] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[11] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[12] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[13] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[14] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[15] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[16] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[17] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[18] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[19] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[20] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[21] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[22] = 273.15 + (71 - 32) * 5.0 / 9.0;
		heatThermostat[23] = 273.15 + (71 - 32) * 5.0 / 9.0;

		// Cooling. Building America Simulation Protocols
		coolThermostat[0] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 00:00 -> 01:00
		coolThermostat[1] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 01:00 -> 02:00
		coolThermostat[2] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 02:00 -> 03:00
		coolThermostat[3] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 03:00 -> 04:00
		coolThermostat[4] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 04:00 -> 05:00
		coolThermostat[5] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 05:00 -> 06:00
		coolThermostat[6] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 06:00 -> 07:00
		coolThermostat[7] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 07:00 -> 08:00
		coolThermostat[8] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 08:00 -> 09:00	
		coolThermostat[9] = 273.15 + (76 - 32) * 5.0 / 9.0;			// 09:00 -> 10:00
		coolThermostat[10] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 10:00 -> 11:00
		coolThermostat[11] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 11:00 -> 12:00
		coolThermostat[12] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 12:00 -> 13:00
		coolThermostat[13] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 13:00 -> 14:00
		coolThermostat[14] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 14:00 -> 15:00
		coolThermostat[15] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 15:00 -> 16:00
		coolThermostat[16] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 16:00 -> 17:00	
		coolThermostat[17] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 17:00 -> 18:00
		coolThermostat[18] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 18:00 -> 19:00
		coolThermostat[19] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 19:00 -> 20:00
		coolThermostat[20] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 20:00 -> 21:00
		coolThermostat[21] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 21:00 -> 22:00
		coolThermostat[22] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 22:00 -> 23:00
		coolThermostat[23] = 273.15 + (76 - 32) * 5.0 / 9.0;		// 23:00 -> 24:00

		//coolThermostat[0] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 00:00 -> 01:00
		//coolThermostat[1] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 01:00 -> 02:00
		//coolThermostat[2] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 02:00 -> 03:00
		//coolThermostat[3] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 03:00 -> 04:00
		//coolThermostat[4] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 04:00 -> 05:00
		//coolThermostat[5] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 05:00 -> 06:00
		//coolThermostat[6] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 06:00 -> 07:00
		//coolThermostat[7] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 07:00 -> 08:00
		//coolThermostat[8] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 08:00 -> 09:00	
		//coolThermostat[9] = 273.15 + (73 - 32) * 5.0 / 9.0;			// 09:00 -> 10:00
		//coolThermostat[10] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 10:00 -> 11:00
		//coolThermostat[11] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 11:00 -> 12:00
		//coolThermostat[12] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 12:00 -> 13:00
		//coolThermostat[13] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 13:00 -> 14:00
		//coolThermostat[14] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 14:00 -> 15:00
		//coolThermostat[15] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 15:00 -> 16:00
		//coolThermostat[16] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 16:00 -> 17:00	
		//coolThermostat[17] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 17:00 -> 18:00
		//coolThermostat[18] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 18:00 -> 19:00
		//coolThermostat[19] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 19:00 -> 20:00
		//coolThermostat[20] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 20:00 -> 21:00
		//coolThermostat[21] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 21:00 -> 22:00
		//coolThermostat[22] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 22:00 -> 23:00
		//coolThermostat[23] = 273.15 + (73 - 32) * 5.0 / 9.0;		// 23:00 -> 24:00

		//// Heating
		//heatThermostat[0] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[1] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[2] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[3] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[4] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[5] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[6] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[7] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[8] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[9] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[10] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[11] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[12] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[13] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[14] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[15] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[16] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[17] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[18] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[19] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[20] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[21] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[22] = 273.15 + (70 - 32) * 5.0 / 9.0;
		//heatThermostat[23] = 273.15 + (70 - 32) * 5.0 / 9.0;

		//// Cooling
		//coolThermostat[0] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 00:00 -> 01:00
		//coolThermostat[1] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 01:00 -> 02:00
		//coolThermostat[2] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 02:00 -> 03:00
		//coolThermostat[3] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 03:00 -> 04:00
		//coolThermostat[4] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 04:00 -> 05:00
		//coolThermostat[5] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 05:00 -> 06:00
		//coolThermostat[6] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 06:00 -> 07:00
		//coolThermostat[7] = 273.15 + (74 - 32) * 5.0 / 9.0;			// 07:00 -> 08:00
		//coolThermostat[8] = 273.15 + (80 - 32) * 5.0 / 9.0;			// 08:00 -> 09:00	// 80 normal
		//coolThermostat[9] = 273.15 + (80 - 32) * 5.0 / 9.0;			// 09:00 -> 10:00
		//coolThermostat[10] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 10:00 -> 11:00
		//coolThermostat[11] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 11:00 -> 12:00
		//coolThermostat[12] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 12:00 -> 13:00
		//coolThermostat[13] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 13:00 -> 14:00
		//coolThermostat[14] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 14:00 -> 15:00
		//coolThermostat[15] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 15:00 -> 16:00
		//coolThermostat[16] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 16:00 -> 17:00	// 74 in pre-cooling 80 normally
		//coolThermostat[17] = 273.15 + (74 - 32) * 5.0 / 9.0;		// 17:00 -> 18:00
		//coolThermostat[18] = 273.15 + (74 - 32) * 5.0 / 9.0;		// 18:00 -> 19:00
		//coolThermostat[19] = 273.15 + (74 - 32) * 5.0 / 9.0;		// 19:00 -> 20:00
		//coolThermostat[20] = 273.15 + (74 - 32) * 5.0 / 9.0;		// 20:00 -> 21:00
		//coolThermostat[21] = 273.15 + (74 - 32) * 5.0 / 9.0;		// 21:00 -> 22:00
		//coolThermostat[22] = 273.15 + (74 - 32) * 5.0 / 9.0;		// 22:00 -> 23:00
		//coolThermostat[23] = 273.15 + (74 - 32) * 5.0 / 9.0;		// 23:00 -> 24:00

		// Heating - HOME ENERGY SAVER 2013 http://www.hes.lbl.gov/consumer/faqs#h8
		//heatThermostat[0] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[1] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[2] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[3] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[4] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[5] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[6] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[7] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[8] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[9] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[10] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[11] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[12] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[13] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[14] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[15] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[16] = 273.15 + (60 - 32) * 5.0 / 9.0;
		//heatThermostat[17] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[18] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[19] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[20] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[21] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[22] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[23] = 273.15 + (60 - 32) * 5.0 / 9.0;

		////// Cooling - HOME ENERGY SAVER 2013 http://www.hes.lbl.gov/consumer/faqs#h8
		//coolThermostat[0] = 273.15 + (78 - 32) * 5.0 / 9.0;			// 00:00 -> 01:00
		//coolThermostat[1] = 273.15 + (78 - 32) * 5.0 / 9.0;			// 01:00 -> 02:00
		//coolThermostat[2] = 273.15 + (78 - 32) * 5.0 / 9.0;			// 02:00 -> 03:00
		//coolThermostat[3] = 273.15 + (78 - 32) * 5.0 / 9.0;			// 03:00 -> 04:00
		//coolThermostat[4] = 273.15 + (78 - 32) * 5.0 / 9.0;			// 04:00 -> 05:00
		//coolThermostat[5] = 273.15 + (78 - 32) * 5.0 / 9.0;			// 05:00 -> 06:00
		//coolThermostat[6] = 273.15 + (75 - 32) * 5.0 / 9.0;			// 06:00 -> 07:00
		//coolThermostat[7] = 273.15 + (75 - 32) * 5.0 / 9.0;			// 07:00 -> 08:00
		//coolThermostat[8] = 273.15 + (75 - 32) * 5.0 / 9.0;			// 08:00 -> 09:00
		//coolThermostat[9] = 273.15 + (80 - 32) * 5.0 / 9.0;			// 09:00 -> 10:00
		//coolThermostat[10] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 10:00 -> 11:00
		//coolThermostat[11] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 11:00 -> 12:00
		//coolThermostat[12] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 12:00 -> 13:00
		//coolThermostat[13] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 13:00 -> 14:00
		//coolThermostat[14] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 14:00 -> 15:00
		//coolThermostat[15] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 15:00 -> 16:00
		//coolThermostat[16] = 273.15 + (80 - 32) * 5.0 / 9.0;		// 16:00 -> 17:00
		//coolThermostat[17] = 273.15 + (75 - 32) * 5.0 / 9.0;		// 17:00 -> 18:00
		//coolThermostat[18] = 273.15 + (75 - 32) * 5.0 / 9.0;		// 18:00 -> 19:00
		//coolThermostat[19] = 273.15 + (75 - 32) * 5.0 / 9.0;		// 19:00 -> 20:00
		//coolThermostat[20] = 273.15 + (75 - 32) * 5.0 / 9.0;		// 20:00 -> 21:00
		//coolThermostat[21] = 273.15 + (75 - 32) * 5.0 / 9.0;		// 21:00 -> 22:00
		//coolThermostat[22] = 273.15 + (75 - 32) * 5.0 / 9.0;		// 22:00 -> 23:00
		//coolThermostat[23] = 273.15 + (78 - 32) * 5.0 / 9.0;		// 23:00 -> 24:00

		// Cooling with Lower Setpoint for Humid CZs 1A and 2A
		//coolThermostat[0] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[1] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[2] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[3] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[4] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[5] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[6] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[7] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[8] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[9] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[10] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[11] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[12] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[13] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[14] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[15] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[16] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[17] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[18] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[19] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[20] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[21] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[22] = 273.15 + (74 - 32) * 5.0 / 9.0;
		//coolThermostat[23] = 273.15 + (74 - 32) * 5.0 / 9.0;

		// Thermostat settings (RESAVE 2012 and T24, ACM 2008)
		// Heating
		//heatThermostat[0] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[1] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[2] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[3] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[4] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[5] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[6] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[7] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[8] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[9] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[10] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[11] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[12] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[13] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[14] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[15] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[16] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[17] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[18] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[19] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[20] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[21] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[22] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[23] = 273.15 + (65 - 32) * 5.0 / 9.0;

		//// Cooling
		//coolThermostat[0] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[1] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[2] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[3] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[4] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[5] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[6] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[7] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[8] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[9] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[10] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[11] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[12] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[13] = 273.15 + (82 - 32) * 5.0 / 9.0;
		//coolThermostat[14] = 273.15 + (81 - 32) * 5.0 / 9.0;
		//coolThermostat[15] = 273.15 + (80 - 32) * 5.0 / 9.0;
		//coolThermostat[16] = 273.15 + (79 - 32) * 5.0 / 9.0;
		//coolThermostat[17] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[18] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[19] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[20] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[21] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[22] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[23] = 273.15 + (78 - 32) * 5.0 / 9.0;

		// Thermostat settings (RESAVE 2012 and T24, ACM 2008) PRECOOLING
		//// Heating
		//heatThermostat[0] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[1] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[2] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[3] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[4] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[5] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[6] = 273.15 + (65 - 32) * 5.0 / 9.0;
		//heatThermostat[7] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[8] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[9] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[10] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[11] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[12] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[13] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[14] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[15] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[16] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[17] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[18] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[19] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[20] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[21] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[22] = 273.15 + (68 - 32) * 5.0 / 9.0;
		//heatThermostat[23] = 273.15 + (65 - 32) * 5.0 / 9.0;

		//// Cooling
		//coolThermostat[0] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[1] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[2] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[3] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[4] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[5] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[6] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[7] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[8] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[9] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[10] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[11] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[12] = 273.15 + (83 - 32) * 5.0 / 9.0;
		//coolThermostat[13] = 273.15 + (75 - 32) * 5.0 / 9.0;
		//coolThermostat[14] = 273.15 + (75 - 32) * 5.0 / 9.0;
		//coolThermostat[15] = 273.15 + (75 - 32) * 5.0 / 9.0;
		//coolThermostat[16] = 273.15 + (79 - 32) * 5.0 / 9.0;
		//coolThermostat[17] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[18] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[19] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[20] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[21] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[22] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//coolThermostat[23] = 273.15 + (78 - 32) * 5.0 / 9.0;
		//// [END] Thermostat settings (RESAVE 2012) ========================================================================================

		// This reads in the filename and climate zone data from a file instead of input by user
		string input_file = simFile[sim];
		string weather_file = climateZone[sim];
		string output_file = outName[sim];

		// Opening moisture output file
		ofstream moistureFile(outPath + output_file + ".hum");
		if(!moistureFile) { 
			cout << "Cannot open: " << outPath + output_file + ".hum" << endl;
			system("pause");
			return 1; 
		}

		moistureFile << "HROUT\tHRattic\tHRreturn\tHRsupply\tHRhouse\tHRmaterials\tRH%house\tRHind60\tRHind70" << endl;

		//moistureFile << "HR_Attic\tHR_Return\tHR_Supply\tHR_House\tHR_Materials" << endl; This is the old format.

		// [START] Read in Building Inputs =========================================================================================================================
		ifstream buildingFile(inPath + input_file + ".csv"); 

		if(!buildingFile) { 
			cout << "Cannot open: " << input_file + ".csv" << endl;
			system("pause");
			return 1; 
		}

		// 'atof' converts a string variable to a double variable
		buildingFile.getline(reading, 255);
		double C = atof(reading);		// Envelope Leakage Coefficient [m3/sPa^n]

		buildingFile.getline(reading, 255);
		double n = atof(reading);		// Envelope Pressure Exponent

		buildingFile.getline(reading, 255);
		double h = atof(reading);		// Eaves Height [m]

		buildingFile.getline(reading, 255);
		double R = atof(reading);		// Ceiling Floor Leakage Sum

		buildingFile.getline(reading, 255);
		double X = atof(reading);		// Ceiling Floor Leakage Difference

		double lc = (R + X) * 50;		// Percentage of leakage in the ceiling
		double lf = (R - X) * 50;		// Percentage of leakage in the floor
		double lw = 100 - lf - lc;		// Percentage of leakage in the walls

		buildingFile.getline(reading, 255);
		int numFlues = atoi(reading);			// Number of flues/chimneys/passive stacks

		for(int i=0; i < numFlues; i++) {
			buildingFile.getline(reading, 255);
			flue[i].flueC = atof(reading);		// Flue leakage

			buildingFile.getline(reading, 255);
			flue[i].flueHeight = atof(reading);	// Flue height [m]

			buildingFile.getline(reading, 255);
			flue[i].flueTemp = atof(reading);	// Flue gas temperature (-99 means temperature in flue changes)
		}

		// =========================== Leakage Inputs ==============================
		for(int i=0; i < 4; i++) {
			buildingFile.getline(reading, 255);
			wallFraction[i] = atof(reading);		// Fraction of leakage in walls 1 (North) 2 (South) 3 (East) 4 (West)

			buildingFile.getline(reading, 255);		// Fraction of leake in floor below wall 1, 2, 3 and 4
			floorFraction[i] = atof(reading);
		}

		buildingFile.getline(reading, 255);
		double flueShelterFactor = atof(reading);	// Shelter factor at the top of the flue (1 if the flue is higher than surrounding obstacles

		buildingFile.getline(reading, 255);
		int numPipes = atoi(reading);				// Number of passive vents but appears to do much the same as flues

		for(int i=0; i < numPipes; i++) {
			buildingFile.getline(reading, 255);
			Pipe[i].wall = atoi(reading);

			buildingFile.getline(reading, 255);
			Pipe[i].h = atof(reading);

			buildingFile.getline(reading, 255);
			Pipe[i].A = atof(reading);

			buildingFile.getline(reading, 255);
			Pipe[i].n = atof(reading);

			buildingFile.getline(reading, 255);
			Pipe[i].Swf = atof(reading);

			buildingFile.getline(reading, 255);
			Pipe[i].Swoff = atof(reading);
		}

		// =========================== Building Inputs =============================
		buildingFile.getline(reading, 255);
		double Hfloor = atof(reading);

		buildingFile.getline(reading, 255);
		string rowOrIsolated = reading;			// House in a row (R) or isolated (any string other than R)
		rowOrIsolated= "R";						// Defaults to R for now

		buildingFile.getline(reading, 255);
		double houseVolume = atof(reading);		// Conditioned volume of house (m3)

		buildingFile.getline(reading, 255);
		double floorArea = atof(reading);		// Conditioned floor area (m2)

		buildingFile.getline(reading, 255);
		double planArea = atof(reading);		// Footprint of house (m2)

		double storyHeight = 2.5;				// Story height (m)

		buildingFile.getline(reading, 255);
		double houseLength = atof(reading);		// Long side of house (m) NOT USED IN CODE

		buildingFile.getline(reading, 255);
		double houseWidth = atof(reading);		// Short side of house (m) NOT USED IN CODE

		buildingFile.getline(reading, 255);
		double UAh = atof(reading);				// Heating U-Value (Thermal Conductance)

		buildingFile.getline(reading, 255);
		double UAc = atof(reading);				// Cooling U-Value (Thermal Conductance)

		// ====================== Venting Inputs (Windows/Doors)====================
		buildingFile.getline(reading, 255);
		int numWinDoor = atoi(reading);

		// These are not currently used in the Excel input generating spreadsheet
		// Add them back in if needed
		/*for(int i=0; i < numWinDoor; i++) {
		buildingFile.getline(reading, 255);
		winDoor[i].wall = atoi(reading);

		buildingFile.getline(reading, 255);
		winDoor[i].High = atof(reading);

		buildingFile.getline(reading, 255);
		winDoor[i].Wide = atof(reading);

		buildingFile.getline(reading, 255);
		winDoor[i].Top = atof(reading);

		buildingFile.getline(reading, 255);
		winDoor[i].Bottom = atof(reading);
		}*/

		// ================== Mechanical Venting Inputs (Fans etc) =================
		buildingFile.getline(reading, 255);
		int numFans = atoi(reading);

		for(int i=0;  i < numFans; i++) {
			buildingFile.getline(reading, 255);
			fan[i].power = atof(reading);

			buildingFile.getline(reading, 255);
			fan[i].q = atof(reading);

			buildingFile.getline(reading, 255);
			fan[i].oper = atof(reading);

			fan[i].on = 0;
		}

		buildingFile.getline(reading, 255);
		double windowWE = atof(reading);

		buildingFile.getline(reading, 255);
		double windowN = atof(reading);

		buildingFile.getline(reading, 255);
		double windowS = atof(reading);

		buildingFile.getline(reading, 255);
		double winShadingCoef = atof(reading);

		buildingFile.getline(reading, 255);
		double ceilRval_heat = atof(reading);

		buildingFile.getline(reading, 255);
		double ceilRval_cool = atof(reading);

		buildingFile.getline(reading, 255);
		double latentLoad = atof(reading);

		buildingFile.getline(reading, 255);
		double internalGains1 = atof(reading);

		// =========================== Attic Inputs ================================
		buildingFile.getline(reading, 255);
		double atticVolume = atof(reading);

		buildingFile.getline(reading, 255);
		double atticC = atof(reading);

		buildingFile.getline(reading, 255);
		double atticPressureExp = atof(reading);

		for(int i=0; i < 5; i++) {
			buildingFile.getline(reading, 255);
			soffitFraction[i] = atof(reading);			
		}
		for(int i=0; i < 4; i++) {
			buildingFile.getline(reading, 255);
			soffit[i].h = atof(reading);			
		}

		// =========================== Attic Vent Inputs ===========================
		buildingFile.getline(reading, 255);
		int numAtticVents = atoi(reading);

		for(int i=0; i < numAtticVents; i++) {
			buildingFile.getline(reading, 255);
			atticVent[i].wall = atoi(reading);

			buildingFile.getline(reading, 255);
			atticVent[i].h = atof(reading);

			buildingFile.getline(reading, 255);
			atticVent[i].A = atof(reading);

			buildingFile.getline(reading, 255);
			atticVent[i].n = atof(reading);
		}

		// =========================== Roof Inputs =================================
		buildingFile.getline(reading, 255);
		double roofPitch = atof(reading);

		buildingFile.getline(reading, 255);
		string roofPeakOrient = reading;		// Roof peak orientation, D = perpendicular to front of house (Wall 1), P = parrallel to front of house

		buildingFile.getline(reading, 255);
		double roofPeakHeight = atof(reading);

		buildingFile.getline(reading, 255);
		int numAtticFans = atoi(reading);

		for(int i = 0; i < numAtticFans; i++) {
			buildingFile.getline(reading, 255);
			atticFan[i].power = atof(reading);

			buildingFile.getline(reading, 255);
			atticFan[i].q = atof(reading);

			buildingFile.getline(reading, 255);
			atticFan[i].oper = atof(reading);
		}

		buildingFile.getline(reading, 255);
		double roofRval = atoi(reading);

		buildingFile.getline(reading, 255);
		int roofType = atoi(reading);

		// =========================== Duct Inputs =================================
		buildingFile.getline(reading, 255);
		double ductLocation = atof(reading);			

		buildingFile.getline(reading, 255);
		double supThickness = atof(reading);

		buildingFile.getline(reading, 255);
		double retThickness = atof(reading);

		buildingFile.getline(reading, 255);
		double supRval = atof(reading);

		buildingFile.getline(reading, 255);
		double retRval = atof(reading);

		buildingFile.getline(reading, 255);
		double supLF0 = atof(reading);						//Supply duct leakage fraction (e.g., 0.01 = 1% leakage).					

		buildingFile.getline(reading, 255);
		double retLF0 = atof(reading);						//Return duct leakage fraction (e.g., 0.01 = 1% leakage)

		buildingFile.getline(reading, 255);
		double supLength = atof(reading);

		buildingFile.getline(reading, 255);
		double retLength = atof(reading);

		buildingFile.getline(reading, 255);
		double supDiameter = atof(reading);

		buildingFile.getline(reading, 255);
		double retDiameter = atof(reading);

		buildingFile.getline(reading, 255);
		double qAH_cool0 = atof(reading);				// Cooling Air Handler air flow (m^3/s)

		buildingFile.getline(reading, 255);
		double qAH_heat0 = atof(reading);				// Heating Air Handler air flow (m^3/s)

		buildingFile.getline(reading, 255);
		double supn = atof(reading);

		buildingFile.getline(reading, 255);
		double retn = atof(reading);

		buildingFile.getline(reading, 255);
		double supC = atof(reading);					//atof(reading); Supply leak flow coefficient

		buildingFile.getline(reading, 255);
		double retC = atof(reading);					//atof(reading); Return leak flow coefficient

		// NOTE: The buried variable is not used but left in code to continue proper file navigation
		buildingFile.getline(reading, 255);
		double buried = atof(reading);

		// =========================== Equipment Inputs ============================

		buildingFile.getline(reading, 255);
		double capacityraw = atof(reading);

		buildingFile.getline(reading, 255);
		double capacityari = atof(reading);

		buildingFile.getline(reading, 255);
		double EERari = atof(reading);

		buildingFile.getline(reading, 255);
		double hcapacity = atof(reading);		// Heating capacity [kBtu/h]

		buildingFile.getline(reading, 255);
		double fanPower_heating0 = atof(reading);		// Heating fan power [W]

		buildingFile.getline(reading, 255);
		double fanPower_cooling0 = atof(reading);		// Cooling fan power [W]

		buildingFile.getline(reading, 255);
		double charge = atof(reading);

		buildingFile.getline(reading, 255);
		double AFUE = atof(reading);			// Annual Fuel Utilization Efficiency for the furnace

		buildingFile.getline(reading, 255);
		int bathroomSchedule = atoi(reading);	// Bathroom schedule file to use (1, 2 or 3)

		buildingFile.getline(reading, 255);
		int numBedrooms = atoi(reading);		// Number of bedrooms (for 62.2 target ventilation calculation)

		buildingFile.getline(reading, 255);
		int numStories = atoi(reading);			// Number of stories in the building (for Nomalized Leakage calculation)

		buildingFile.getline(reading, 255);
		double weatherFactor = atof(reading);	// Weather Factor (w) (for infiltration calculation from ASHRAE 136)

		buildingFile.getline(reading, 255);
		double rivecFlagInd = atof(reading);	// Indicator variable that instructs a fan code 13 or 17 to be run by RIVEC controls. 1= yes, 0=no. Brennan.

		//The variable from here down wree added by Brennan as part of the Smart Ventilation Humidity Control project

		buildingFile.getline(reading, 255);
		double HumContType = atof(reading);	// Type of humidity control to be used in the RIVEC calculations. 1,2,...n Brennan.

		buildingFile.getline(reading, 255);
		double wCutoff = atof(reading);	// Humidity Ratio cut-off calculated as some percentile value for the climate zone. Brennan.

		buildingFile.getline(reading, 255);
		double wDiffMaxNeg = atof(reading);	// Maximum average indoor-outdoor humidity differene, when wIn < wOut. Climate zone average.

		buildingFile.getline(reading, 255);
		double wDiffMaxPos = atof(reading);	// Maximum average indoor-outdoor humidity differene, when wIn > wOut. Climate zone average.

		buildingFile.getline(reading, 255); //25th percentiles for each month of the year, per TMY3
		double W25_1 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W25_2 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W25_3 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W25_4 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W25_5 = atof(reading);

		buildingFile.getline(reading, 255);
		double W25_6 = atof(reading);

		buildingFile.getline(reading, 255);
		double W25_7 = atof(reading);

		buildingFile.getline(reading, 255);
		double W25_8 = atof(reading);

		buildingFile.getline(reading, 255);
		double W25_9 = atof(reading);

		buildingFile.getline(reading, 255);
		double W25_10 = atof(reading);

		buildingFile.getline(reading, 255);
		double W25_11 = atof(reading);

		buildingFile.getline(reading, 255);
		double W25_12 = atof(reading);
				
		buildingFile.getline(reading, 255); //75th percentiles for each month of the year, per TMY3
		double W75_1 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W75_2 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W75_3 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W75_4 = atof(reading);
				
		buildingFile.getline(reading, 255);
		double W75_5 = atof(reading);

		buildingFile.getline(reading, 255);
		double W75_6 = atof(reading);

		buildingFile.getline(reading, 255);
		double W75_7 = atof(reading);

		buildingFile.getline(reading, 255);
		double W75_8 = atof(reading);

		buildingFile.getline(reading, 255);
		double W75_9 = atof(reading);

		buildingFile.getline(reading, 255);
		double W75_10 = atof(reading);

		buildingFile.getline(reading, 255);
		double W75_11 = atof(reading);

		buildingFile.getline(reading, 255);
		double W75_12 = atof(reading);

		buildingFile.close();

		// [END] Read in Building Inputs ============================================================================================================================================


		// In case the user enters leakage fraction in % rather than as a fraction
		if(supLF0 > 1)
			supLF0 = supLF0 / 100;
		if(retLF0 > 1)
			retLF0 = retLF0 / 100;

		double supCp = 753.624;										// Specific heat capacity of steel [j/kg/K]
		double suprho = 16.018 * 2;									// Supply duct density. The factor of two represents the plastic and sprical [kg/m^3]
		double retCp = 753.624;										// Specific heat capacity of steel [j/kg/K]
		double retrho = 16.018 * 2;									// Return duct density [kg/m^3]
		double supArea = (supDiameter + 2 * supThickness) * pi * supLength;		// Surface area of supply ducts [m2]
		double retArea = (retDiameter + 2 * retThickness) * pi * retLength;		// Surface area of return ducts [m2]
		double supVolume = (pow(supDiameter, 2) * pi / 4) * supLength;			// Volume of supply ducts [m3]
		double retVolume = (pow(retDiameter, 2) * pi / 4) * retLength;			// Volume of return ducts [m3]
		hcapacity = hcapacity * .29307107 * 1000 * AFUE;			// Heating capacity of furnace converted from kBtu/hr to Watts and with AFUE adjustment
		double MWha = .5 * floorArea / 186;							// MWha is the moisture transport coefficient that scales with floor area (to scale with surface area of moisture)
					
		// [START] Filter Loading ==================================================================================
		
		// Inputs to set filter type and loading rate
		int filterLoadingFlag = 0;		// Filter loading flag = 0 (OFF) or 1 (ON)
		int MERV = 5;					// MERV rating of filter (may currently be set to 5, 8, 11 or 16)
		int loadingRate = 0;			// loading rate of filter, f (0,1,2) = (low,med,high)
		int BPMflag = 0;				// BPM (1) or PSC (0) air handler motor

		int filterChanges = 0;			// Number of filters used throughout the year
		double qAH_low = 0;				// Lowest speed of AH (set in sub_filterLoading)

		// Set fan power and airflow rates to initial (0) input values for filter loading
		double qAH_heat = qAH_heat0;
		double qAH_cool = qAH_cool0;
		double fanPower_heating = fanPower_heating0;
		double fanPower_cooling = fanPower_cooling0;
		double retLF = retLF0;
		double supLF = supLF0;

		double massFilter_cumulative = 0;	// Cumulative mass that has flown through the filter
		double massAH_cumulative = 0;		// Cumulative mass that has flown through the AH

		// Filter loading coefficients (initialization- attributed values in sub_filterLoading)
		double A_qAH_heat = 0;			// Initial AH airflow change (in heating mode) from installing filter [%]
		double A_qAH_cool = 0;			// Initial AH airflow change (in cooling mode) from installing filter [%]
		double A_wAH_heat = 0;			// Initial AH power change (in heating mode) from installing filter [%]
		double A_wAH_cool = 0;			// Initial AH power change (in cooling mode) from installing filter [%]
		double A_DL = 0;				// Intial return duct leakage change from installing filter [%]
		
		double k_qAH = 0;				// Gradual change in AH airflow from filter loading [% per 10^6kg of air mass through filter]
		double k_wAH = 0;				// Gradual change in AH power from filter loading [% per 10^6kg of air mass through filter]
		double k_DL = 0;				// Gradual change in return duct leakage from filter loading [% per 10^6kg of air mass through filter]
		
		// Open filter loading file
		ofstream filterFile(outPath + output_file + ".fil");
		if(!filterFile) { 
			cout << "Cannot open: " << outPath + output_file + ".fil" << endl;
			system("pause");
			return 1; 
		}

		filterFile << "mAH_cumu\tqAH\twAH\tretLF" << endl;
		
		// Filter loading coefficients are in the sub_filterLoading sub routine
		if(filterLoadingFlag == 1)
			sub_filterLoading(MERV, loadingRate, BPMflag, A_qAH_heat, A_qAH_cool, A_wAH_heat, A_wAH_cool, A_DL, k_qAH, k_wAH, k_DL, qAH_heat0, qAH_cool0, qAH_low);
		// [END] Filter Loading ====================================================================================

		// Cooling capacity air flow correction term (using CFM)
		double qAH_cfm = qAH_cool / .0004719;
		double qAHcorr = 1.62 - .62 * qAH_cfm / (400 * capacityraw) + .647 * log(qAH_cfm / (400 * capacityraw));	

		// Occupancy and dynamic schedule flags
		int dynamicScheduleFlag = 1;	// 1 = use dynamic fan schedules, 0 = do not use dynamic fan schedules

		// For RIVEC calculations
		int peakFlag = 0;				// Peak flag (0 or 1) prevents two periods during same day
		int rivecFlag = 0;				// Dose controlled ventilation (RIVEC) flag 0 = off, 1 = use rivec (mainly for HRV/ERV control)
		double qRivec = 1.0;			// Airflow rate of RIVEC fan for max allowed dose and exposure [L/s]
		int numFluesActual = numFlues;	// numFluesActual used to remember the number of flues for when RIVEC blocks them off as part of a hybrid ventilation strategy

		// For Economizer calculations
		int economizerUsed = 0;			// 1 = use economizer and change C (house leakage) for pressure relief while economizer is running (changes automatically)
		double Coriginal = 0;			// House leakage before pressure relief opens for economizer
		double ELAeconomizer = 0;		// Size of leakage increase for economizer pressure relief
		double Ceconomizer = 0;			// Total leakage of house inncluding extra economizer pressure relief (only while economizer is running)

		for(int i=0; i < numFans; i++) {
			if(fan[i].oper == 21 || fan[i].oper == 22)
				economizerUsed = 1;		// Lets REGCAP know that an economizer is being used
				//economizerUsed = 0;		// Force economizer to be off
			if(fan[i].oper == 23 || fan[i].oper == 24 || fan[i].oper == 25 || fan[i].oper == 26 || fan[i].oper == 27)
				dynamicScheduleFlag = 1;
			if(fan[i].oper == 30 || fan[i].oper == 31 || fan[i].oper == 50 || fan[i].oper == 51) { // RIVEC fans
				qRivec = -1 * fan[i].q	* 1000;
				rivecFlag = 1;
			}
		}

		for(int i=0; i < numFans; i++) {
			if(fan[i].oper == 5 || fan[i].oper == 16) {	// If using an HRV (5 or 16) with RIVEC. Avoids a divide by zero
				qRivec = -1 * fan[i].q * 1000;
			}
			if(fan[i].oper == 13 && rivecFlagInd == 1 || fan[i].oper == 17 && rivecFlagInd == 1){ //If using CFIS or ERV+AHU with RIVEC control.
					rivecFlag = 1;
			}
		}

		// Presure Relief for Economizer, increase envelope leakage C while economizer is operating
		if(economizerUsed == 1) {
			Coriginal = C;
			if(qAH_cool >= qAH_heat) {			// Dependent on the largest of the heating/cooling AH fan power
				// sized to 2Pa of pressure while economizer running
				ELAeconomizer = qAH_cool * (sqrt(airDensityRef / (2 * 2)) / 1);
				Ceconomizer = 1 * ELAeconomizer * sqrt(2 / airDensityRef) * pow(4, (.5 - .65));
			} else {
				ELAeconomizer = qAH_heat * (sqrt(airDensityRef / (2 * 2)) / 1);
				Ceconomizer = 1 * ELAeconomizer * sqrt(2 / airDensityRef) * pow(4, (.5 - .65));
			}
		}

		// [START] Read in Shelter Values ================================================================================================
		// (reading urban shelter values from a data file: Bshelter.dat)
		// Computed for the houses at AHHRF for every degree of wind angle

		ifstream shelterFile(shelterFile_name); 
		if(!shelterFile) { 
			cout << "Cannot open: " << shelterFile_name << endl;
			system("pause");
			return 1; 
		}

		// FF: This angle variable is being overwritten to read Swinit in proper ductLocation per FOR iteration. Not used in code.
		double angle;
		for(int i=0; i < 361; i++) {
			shelterFile >> angle >> Swinit[0][i] >> Swinit[1][i] >> Swinit[2][i] >> Swinit[3][i];
		}

		shelterFile.close();
		// [END] Read in Shelter Values ================================================================================================

		// [START] Terrain ============================================================================================
		// Terrain where the house is located (for wind shelter etc.) See ASHRAE Fundamentals 2009 F24.3
		double windPressureExp;		// Power law exponent of the wind speed profile at the building site
		double layerThickness;		// Atmospheric boundary layer thickness [m]

		int terrain = 2;	// 1 = large city centres, 2 = urban and suburban, 3 = open terrain, 4 = open sea

		switch (terrain) {
		case 1:
			// 1. Large city centres, at least 50% of buildings are higher than 25m
			windPressureExp = 0.33;
			layerThickness = 460;
			break;

		case 2:
			// 2. Urban and suburban areas, wooded areas
			windPressureExp = 0.22;
			layerThickness = 370;
			break;

		case 3:
			// 3. Open terrain with scattered obsructions e.g. meteorlogical stations
			windPressureExp = 0.14;
			layerThickness = 270;
			break;
		case 4:
			// 4. Completely flat e.g. open water
			windPressureExp = 0.10;
			layerThickness = 210;
			break;
		}

		// Wind speed correction to adjust met wind speed to that at building eaves height
		double metExponent = 0.14;		// Power law exponent of the wind speed profile at the met station
		double metThickness = 270;		// Atmospheric boundary layer thickness at met station [m]
		double metHeight = 10;			// Height of met station wind measurements [m]
		double windSpeedCorrection = pow((metThickness / metHeight), metExponent) * pow((h / layerThickness), windPressureExp);
		//double windSpeedCorrection = pow((80/ 10), .15) * pow((h / 80), .3);		// The old wind speed correction
		// [END] Terrain ============================================================================================

		double ceilingC = C * (R + X) / 2;

		// AL4 is used to estimate flow velocities in the attic
		double AL4 = atticC * sqrt(airDensityRef / 2) * pow(4, (atticPressureExp - .5));

		// New char velocity for unvented attics
		if(AL4 == 0)
			AL4 = ceilingC * sqrt(airDensityRef / 2) * pow(4, (atticPressureExp - .5));

		// AL5 is use to estimate flow velocities in the house
		double AL5 = C * sqrt(airDensityRef / 2) * pow(4, (n - .5));

		// ================= CREATE OUTPUT FILE =================================================
		ofstream outputFile(outPath + output_file + ".rco"); 
		if(!outputFile) { 
			cout << "Cannot open: " << outPath + output_file + ".rco" << endl;
			system("pause");
			return 1; 
		}

		// 5 HR nodes
		// Node 1 is attic air (Node HR[0])
		// Node 2 is return air (Node HR[1])
		// Node 3 is supply air (Node HR[2])
		// Node 4 is house air (Node HR[3])
		// Node 5 is house materials that interact with house air only (Node HR[4])

		// Write output file headers

		outputFile << "Time\tMin\twindSpeed\ttempOut\ttempHouse\tsetpoint\ttempAttic\ttempSupply\ttempReturn\tAHflag\tAHpower\tHcap\tcompressPower\tCcap\tmechVentPower\tHR\tSHR\tMcoil\thousePress\tQhouse\tACH\tACHflue\tventSum\tnonRivecVentSum\tfan1\tfan2\tfan3\tfan4\tfan5\tfan6\tfan7\trivecOn\tturnover\trelExpRIVEC\trelDoseRIVEC\toccupiedExpReal\toccupiedDoseReal\toccupied\toccupiedExp\toccupiedDose\tDAventLoad\tMAventLoad\tHROUT\tHRhouse\tRH%house\tRHind60\tRHind70" << endl; 


		// ================== OPEN WEATHER FILE FOR INPUT ========================================
		ifstream weatherFile(weatherPath + weather_file + ".ws3");		// WS3 for updated TMY3 weather files
		//ifstream weatherFile(weatherPath + weather_file + ".ws2");	// WS2 for outdated TMY2 weather files
		if(!weatherFile) { 
			cout << "Cannot open: " << weatherPath + weather_file + ".ws3" << endl;
			system("pause");
			return 1; 
		}

		// Read in first line of weather file as latitude and altitude
		double latitude, altitude;
		weatherFile >> latitude >> altitude;

		// set 1 = air handler off, and house air temp below setpoint minus 0.5 degrees
		// set 0 = air handler off, but house air temp above setpoint minus 0.5 degrees
		int set = 0;

		int AHflag = 0;			// Air Handler Flag (0/1/2 = OFF/HEATING MODE/COOLING MODE, 100 = fan on for venting, 102 = heating cool down for 1 minute at end of heating cycle)
		int AHflagPrev = 0;
		int econoFlag = 0;		// Economizer Flag (0/1 = OFF/ON)

		// ---------------- Attic and duct system heat transfer nodes --------------------
		for(int k=0; k < 16; k++) {
			tempOld[k] = airTempRef;
		}
		tempOld[2] = 278;
		tempOld[4] = 278;

		double tempAttic = tempOld[0];
		double tempReturn = tempOld[11];
		double tempSupply = tempOld[14];
		double tempHouse = tempOld[15];

		// Initialise time keeping variables
		int minute_day = 0;				// Minute number in the current day (1 to 1440)
		int minute_hour = 0;			// Minute number in the current hour (1 to 60)

		// Setting initial values of air mass for moisture balance:
		double M1 = atticVolume * airDensityRef;		// Mass of attic air
		double M12 = retVolume * airDensityRef;			// Mass of return air
		double M15 = supVolume * airDensityRef;			// Mass of supply air
		double M16 = houseVolume * airDensityRef;		// Mass of house air
		double Mw5 = 60 * floorArea;					// Active mass of moisture in the house (empirical)

		// Like the mass transport coefficient, the active mass for moisture scales with floor area

		// The following are defined for RIVEC ==============================================================================================================
		// Start and end times for the RIVEC base, peak and recovery periods [h]
		int baseStart = 0;
		int baseEnd = 0;
		int peakStart = 0;
		int peakEnd = 0;
		int recoveryStart = 0;
		int recoveryEnd = 0;

		double ELA = C * sqrt(airDensityRef / 2) * pow(4, (n - .5));			// Effective Leakage Area
		double NL = 1000 * (ELA / floorArea) * pow(numStories, .3);				// Normalized Leakage Calculation. Iain! Brennan! this does not match 62.2-2013 0.4 exponent assumption.
		double wInfil = weatherFactor * NL;										// Infiltration credit from updated ASHRAE 136 weather factors [ACH]
		double defaultInfil = .001 * ((floorArea / 100) * 10) * 3600 / houseVolume;	// Default infiltration credit [ACH] (ASHRAE 62.2, 4.1.3 p.4). This NO LONGER exists in 62.2-2013

		double rivecX = (.05 * floorArea + 3.5 * (numBedrooms + 1)) / qRivec;
		double rivecY = 0;

		double Q622 = .001 * (.05 * floorArea + 3.5 * (numBedrooms + 1)) * (3600 / houseVolume); // ASHRAE 62.2 Mechanical Ventilation Rate [ACH]
		
		double Aeq = 0;		// Equivalent air change rate of house to meet 62.2 minimum for RIVEC calculations. Choose one of the following:

		int AeqCalcs = 1; //Brennan. ALWAYS use option 1 (or 4 for existing home with flow deficit), to do 62.2-2013 Aeq calculation. Options 2 and 3 are for 62.2-2010.

		switch (AeqCalcs) {
			// 1. 62.2-2013 ventilation rate with no infiltration credit [ACH]. Brennan
		case 1:
			Aeq = .001 * (.15 * floorArea + 3.5 * (numBedrooms + 1)) * (3600 / houseVolume); //Brennan, equals Qtot from 62.2-2013 in air changes per hour
			rivecY = 0;
			break;
			// 2. 62.2 ventilation rate + infiltration credit from weather factors (w x NL)  [ACH]
		case 2:
			Aeq = .001 * (.05 * floorArea + 3.5 * (numBedrooms + 1)) * (3600 / houseVolume) + wInfil;
			rivecY = (wInfil * houseVolume / 3.6)/qRivec;
			break;
			// 3. 62.2 ventilation rate + 62.2 default infiltration credit (62.2, 4.1.3 p.4)  [ACH]
		case 3:
			Aeq = .001 * (.05 * floorArea + 3.5 * (numBedrooms + 1) + (floorArea / 100) * 10) * (3600 / houseVolume);
			rivecY = ((floorArea / 100) * 10)/qRivec;
			break;
			// 4. 62.2-2013 ventilation rate with no infiltration credit [ACH], with Existing home deficit of 65 l/s. Brennan
		case 4:
			Aeq = .001 * (.15 * floorArea + 3.5 * (numBedrooms + 1)+(65/4)) * (3600 / houseVolume); //Brennan, in VentTempControl simulations, this equals an AER 0.319 hr-1 and equals Qtot PLUS an existing home flow deficit of 65 L/s ((65/4).
			rivecY = 0;
			break;
		}

		double expLimit = 1 + 4 * (1 - rivecX) / (1 + rivecY);	// Exposure limit for RIVEC algorithm. This is the Max Sherman way of calculating the maximum limit. 
		//We have moved away from using this, and instead just use a value of 2.5, based on ratios of chronic to acute pollutant exposure limits.

		long int rivecMinutes = 0;					// Counts how many minutes of the year that RIVEC is on
		double relDose = 1;							// Initial value for relative dose used in the RIVEC algorithm
		double relExp = 1;							// Initial value for relative exposure used in the RIVEC algorithm
		double turnover = 1 / Aeq;					// Initial value for turnover time (hrs) used in the RIVEC algorithm. Turnover is calculated the same for occupied and unoccupied minutes.					
		double dtau = 60;							// RIVEC, one minute timestep (in seconds)
		double rivecdt = dtau / 3600;				// Rivec timestep is in hours, dtau is simulation timestep in seconds. Used in calculation of relative dose and exposure.

		//For calculating the "real" exposure and dose, based on the actual air change of the house predicted by the mass balance. Standard exposure and dose use the sum of annual average infiltration and current total fan airflow.
		double relDoseReal = 1;						// Initial value for relative dose using ACH of house, i.e. the real rel dose not based on ventSum
		double relExpReal = 1;						// Initial value for relative exposure using ACH of house i.e. the real rel exposure not based on ventSum
		double turnoverReal = 1 / Aeq;				// Initial value for real turnover using ACH of house



		// for calculating the dose and exposure based on the hours of occupancy
		long int occupiedMinCount = 0;				// Counts the number of minutes in a year that the house is occupied
		double occupiedDose = 0;		// Relative dose only while occupants are present in the house. IF always occupied, this is equal to relDose
		double occupiedExp = 0;			// Relative exposure only while occupants are present in the house. IF always occupied, this is equal to relExp
		double occupiedDoseReal = 0;	// "Real" relative dose only while occupants are present in the house, based an ACH predicted by mass balance. IF always occupied, this is equal to relDoseReal
		double occupiedExpReal = 0;		// "Real" relative exposure only while occupants are present in the house, based an ACH predicted by mass balance. IF always occupied, this is equal to relExpReal


		//For calculating the annual values of relative dose and exposure, "real" and "occupied". These include cumulative sums and annual averages.
		double meanRelExp = 0;						// Mean relative exposure over the year, used as a cumulative sum and then ultimately annual average value	
		double meanRelDose = 0;						// Mean relative dose over the year, used as a cumulative sum and then ultimately annual average value

		double meanRelExpReal = 0;					//Mean "real" relative exposure over the year, used as a cumulative sum and then ultimately annual average value
		double meanRelDoseReal = 0;					//Mean "real" relative dose over the year, used as a cumulative sum and then ultimately annual average value

		double totalOccupiedExpReal = 0;			//Cumulative sum 
		double meanOccupiedExpReal = 0;				//Annual average

		double totalOccupiedDoseReal = 0;			// Cumulative sum for "real" calculations. total dose and exp over the occupied time period
		double meanOccupiedDoseReal = 0;			// Mean for "real" calculations. total dose and exp over the occupied time period

		double totalOccupiedExp = 0;				//Cumulative sum for OccupiedExp
		double meanOccupiedExp = 0;					// Mean occupied relative exposure over the year

		double totalOccupiedDose = 0;				//Cumulative sum for OccupiedDose
		double meanOccupiedDose = 0;				// Mean occupied relative dose over the year






		double turnoverRealOld = 0;
		double relDoseRealOld = 0;
		
		double relExpTarget = 1;		//This is a relative expsoure value target used for humidity control simulations. It varies between 0 and 2.5, depending on magnitude of indoor-outdoor humidity ratio difference.


		//if(weather_file == "Orlando"){ //For seasonal humidity controller #6 ONLY, we set the dose/exposure to the target values, in order to do longer dose calculation periods (720 hours). 
		//	relDose = 0.46;
		//	relExp = 0.46;
		//} if(weather_file == "Charleston"){
		//	relDose = 0.72;
		//	relExp = 0.72;
		//} if(weather_file == "07"){ //Baltimore
		//	relDose = 0.876;
		//	relExp = 0.876;
		//} if(weather_file == "01"){ //Miami
		//	relDose = 0.46;
		//	relExp = 0.46;
		//} if(weather_file == "02"){ //Houston
		//	relDose = 0.61;
		//	relExp = 0.61;
		//} if(weather_file == "04"){ //Memphis
		//	relDose = 0.72;
		//	relExp = 0.72;
		//}

		// =====================================================================================================================

		// [START] Fan Schedule Inputs =========================================================================================
		// Read in fan schedule (lists of 1s and 0s, 1 = fan ON, 0 = fan OFF, for every minute of the year)
		// Different schedule file depending on number of bathrooms
		string fanSchedule = "no_fan_schedule";
		ifstream fanschedulefile;

		if(dynamicScheduleFlag == 1) {
			switch (bathroomSchedule) {
			case 1:
				fanSchedule = fanSchedulefile_name1;			// Two bathroom fans, 4 occupants, using a dynamic schedule
				break;
			case 2:
				fanSchedule = fanSchedulefile_name2;			// Three bathroom fans, 4 occupants, using dynamic schedules
				break;
			case 3:
				fanSchedule = fanSchedulefile_name3;			// Three bathroom fans, 5 occupants, using dynamic schedules
				break;
			}

			fanschedulefile.open(fanSchedule + ".txt"); 
			if(!fanschedulefile) { 
				cout << "Cannot open: " << fanSchedule + ".txt" << endl;
				system("pause");
				return 1; 
			}			
		}


		// [END] Fan Schedule Inputs =======================================================================================
		int AHminutes;
		int target;
		int day = 1;
		int idirect;
		int solth;
		int dryerFan = 0;		// Dynamic schedule flag for dryer fan (0 or 1)
		int kitchenFan = 0;		// Dynamic schedule flag for kitchen fan (0 or 1)
		int bathOneFan = 0;		// Dynamic schedule flag for first bathroom fan (0 or 1)
		int bathTwoFan = 0;		// Dynamic schedule flag for second bathroom fan (0 or 1)
		int bathThreeFan = 0;	// Dynamic schedule flag for third bathroom fan (0 or 1)
		int HOUR;				// Hour of the day
		//int ttime;			// replaced with the HOUR variable instead of having a separate counter just for thermostats
		int weekendFlag;
		int occupied[24] = {0};	// Used for setting which hours of the day the house is occupied (1) or vacant (0)
		int compTime = 0;
		int compTimeCount = 0;
		int rivecOn = 0;		// 0 (off) or 1 (on) for RIVEC devices
		int mainIterations;
		int Crawl = 0;
		int ERRCODE = 0;
		int economizerRan = 0;	// 0 or else 1 if economizer has run that day
		int hcFlag = 1;
		int FirstCut = 0; //Monthly Indexes assigned based on climate zone
		int SecondCut = 0; //Monthly Indexes assigned based on climate zone

		double pRef;				// Outdoor air pressure read in from weather file
		double sc;
		double weatherTemp;			// Outdoor air temperature read in from weather file [C]. Brennan.
		double tempOut;				// Outdoor temperature converted to Kelvin [K]
		double HROUT;				// Outdoor humidity ratio read in from weather file [kg/kg]
		double windSpeed;			// Wind speed read in from weather file [m/s]
		double direction;			// Wind direction read in from weather file
		double mFanCycler;
		double hcap;				// Heating capacity of furnace (or gas burned by furnace)
		double mHRV =0;				// Mass flow of stand-alone HRV unit
		double mHRV_AH = 0;			// Mass flow of HRV unit integrated with the Air Handler
		double HRV_ASE = 0.82;		// Apparent Sensible Effectiveness of HRV unit
		double mERV_AH = 0;			// Mass flow of stand-alone ERV unit
		double ERV_SRE = 0.63;		// Sensible Recovery Efficiency of ERV unit. SRE and TRE based upon averages from ERV units in HVI directory, as of 5/2015.
		double ERV_TRE = 0.51;		// Total Recovery Efficiency of ERV unit, includes humidity transfer for moisture subroutine		
		double fanHeat;
		double ventSumIN;			// Sum of all ventilation flows into house
		double ventSumOUT;			// Sum of all ventilation flows out from house
		double turnoverOld;			// Turnover from the pervious time step
		double relDoseOld;			// Relative Dose from the previous time step
		double nonRivecVentSumIN;	// Ventilation flows into house not including the RIVEC device flow
		double nonRivecVentSumOUT;	// Ventilation flows out from house not including the RIVEC device flow
		double nonRivecVentSum;		// Equals the larger of nonRivecVentSumIN or nonRivecVentSumOUT
		double qAH;					// Air flowrate of the Air Handler (m^3/s)
		double UA;
		double rceil;
		double econodt = 0;			// Temperature difference between inside and outside at which the economizer operates
		double supVelAH;			// Air velocity in the supply ducts
		double retVelAH;			// Air velocity in the return ducts
		double qSupReg;				// Airflow rate in the supply registers
		double qRetReg;				// Airflow rate in the return registers
		double qRetLeak;			// Leakage airflow rate in the return ducts
		double qSupLeak;			// Leakage airflow rate in the supply ducts
		double mSupLeak1;
		double mRetLeak1;
		double mSupReg1;
		double mRetReg1;
		double mAH1;
		double mSupReg;
		double mAH;					// Mass flow of air through air handler
		double mRetLeak;			// Mass flow of the air that leaks from the attic into the return ducts
		double mSupLeak;			// Mass flow of the air that leaks from the supply ducts into the attic
		double mRetReg;
		double supVel;				// Supply air velocity
		double retVel;				// Return air velocity
		double AHfanPower;
		double AHfanHeat;
		double mSupAHoff=0;
		double mRetAHoff=0;
		double evapcap = 0;
		double latcap = 0;
		double capacity = 0;
		double capacityh = 0;
		double powercon = 0;
		double compressorPower = 0;
		double capacityc = 0;
		double Toutf = 0;
		double tretf = 0;
		double dhret = 0;
		double chargecapd = 0;
		double chargeeerd = 0;
		double EER = 0;
		double chargecapw = 0;
		double chargeeerw = 0;
		double Mcoilprevious = 0;
		double mCeilingOld;
		double limit;
		double matticenvout = 0;
		double mCeiling = 0;
		//double mretahaoff;
		double matticenvin = 0;
		double mHouseIN = 0;
		double mCeilingIN = 0; //Ceiling mass flows, not including register flows. Brennan added for ventilation load calculations.
		double mHouseOUT = 0;
		//double RHOATTIC;
		double internalGains;
		double tempCeiling;
		double tempAtticFloor;
		double tempInnerSheathS;
		double tempOuterSheathS;
		double tempInnerSheathN;
		double tempOuterSheathN;
		double tempInnerGable;
		double tempOuterGable;
		double tempWood;
		double tempRetSurface;
		double tempSupSurface;
		double tempHouseMass;
		double flag;				// Should be an int?
		double mIN = 0;
		double mOUT = 0;
		double Pint = 0;
		double mFlue = 0;
		double dPflue = 0;
		double dPceil = 0;
		double dPfloor = 0;
		double Patticint = 0;
		double mAtticIN = 0;
		double mAtticOUT = 0;
		double TSKY = 0;
		double w = 0;
		double solgain = 0;
		double mHouse = 0;
		double qHouse = 0;
		double houseACH = 0;
		double flueACH = 0;
		double ventSum = 0;
		//double ventsumD = 0;
		//double ventSumOUTD = 0;
		//double ventSumIND = 0;
		double Dhda = 0; //Dry-air enthalpy difference, non-ceiling flows [kJ/kg]. Brennan added these elements for calculating ventilation loads.
		double Dhma = 0; //Moist-air enthalpy difference, non-ceiling flows [kJ/kg]. Brennan added these elements for calculating ventilation loads.
		double ceilingDhda = 0; //Dry-air enthalpy difference, ceiling flows [kJ/kg]. Brennan added these elements for calculating ventilation loads.
		double ceilingDhma = 0; //Moist-air enthalpy difference, ceiling flows [kJ/kg]. Brennan added these elements for calculating ventilation loads.
		double DAventLoad = 0; //Dry-air enthalpy load [kJ/min]. Brennan added these elements for calculating ventilation loads.
		double MAventLoad = 0; //Moist-air enthaply load [kJ/min]. Brennan added these elements for calculating ventilation loads.
		double TotalDAventLoad = 0; //Brennan added these elements for calculating ventilation loads [kJ].
		double TotalMAventLoad = 0; //Brennan added these elements for calculating ventilation loads [kJ].

		vector<double> averageTemp (0);	// Array to track the 7 day running average

		//Variables Brennan added for Temperature Controlled Smart Ventilation.
		double dailyCumulativeTemp = 0;
		double dailyAverageTemp = 0;
		double runningAverageTemp = 0;
		double AIM2 = 0; //Brennan added
		double AEQaim2FlowDiff = 0; //Brennan added
		double FlowDiff = 0; //Brennan added
		double qFanFlowRatio = 0; //Brennan added
		double FanQ = fan[0].q; //Brennan's attempt to fix the airflow outside of the if() structures in the fan.oper section. fan[0].q was always the whole house exhaust fan, to be operated continuously or controlled by temperature controls.
		double FanP = fan[0].power; //Brennan's attempt to fix the fan power outside of the if() structures in the fan.oper section.

		//Variables Brennan added for Smart Ventilatoin Humidity Control.
		double doseTarget = 0.9; //Targeted dose value based on the climate zone file.
		double HiDose = 1.5; //Variable high dose value for real-time humidity control, based on worst-case large, low-occupancy home. 
		int HiMonths[3] = {0, 0, 0};
		int LowMonths[3] = {0, 0, 0};
		double HiMonthDose = 1;
		double LowMonthDose = 1;
		double W25 = 0; //25th percentile control, for outdoor humidity sensor control #14
		double W75 = 0; //75th percentile control, for outdoor humidity sensor control #14
		


		
		// ==============================================================================================
		// ||				 THE SIMULATION LOOP FOR MINUTE-BY-MINUTE STARTS HERE:					   ||
		// ==============================================================================================
		do {
			if(minute_day == 1440) {			// Resetting number of minutes into day every 24 hours
				//if(economizerRan == 1) {
				//}
				minute_day = 0;
				peakFlag = 0;
			}

			if(minute_hour == 60)
				minute_hour = 0;				// Resetting number of minutes into hour

			minute_hour++;						// Minute count (of hour)

			if(minute_hour == 1) {
				AHminutes = 0;					// Resetting air handler operation minutes for this hour
				mFanCycler = 0;					// Fan cycler?
			}

			target = minute_hour - 40;			// Target is used for fan cycler operation currently set for 20 minutes operation, in the last 20 minutes of the hour.

			if(target < 0)
				target = 0;						// For other time periods, replace the "40" with 60 - operating minutes

			hcap = 0;							// Heating Capacity
			mechVentPower = 0;					// Mechanical Vent Power
			mHRV = 0;							// Mass flow of HRV
			mHRV_AH = 0;						// Mass flow of HRV synced to Air Handler
			fanHeat = 0;

			/*if (minute_day == 720)	{// 12:00pm
			//if (minute_day == 780)	// 01:00pm
			//if (minute_day == 660)	// 11:00am				
				economizerRan = 0;  // Resets whether the economizer has operated that day- used to prevent heating from turning on
			}*/
			
			// Resets leakage in case economizer has operated previously (increased leakage for pressure relief)
			if(economizerUsed == 1) {
				C = Coriginal;
				ceilingC = C * (R + X) / 2;
				AL4 = atticC * sqrt(airDensityRef / 2) * pow(4, (atticPressureExp - .5));

				if(AL4 == 0)
					AL4 = ceilingC * sqrt(airDensityRef / 2) * pow(4, (atticPressureExp - .5));

				AL5 = C * sqrt(airDensityRef / 2) * pow(4, (n - .5));
			}
						
			// [START] Filter loading calculations ===============================================================
			if(filterLoadingFlag == 1) {	// Perform filter loading calculations if flag set to 1
				// Air handler airflow rate alterations due to filter loading
				qAH_heat = qAH_heat0 * A_qAH_heat + (k_qAH/100) * qAH_heat0 * massFilter_cumulative;
				qAH_cool = qAH_cool0 * A_qAH_cool + (k_qAH/100) * qAH_cool0 * massFilter_cumulative;
				
				// AH power alterations due to filter loading
				fanPower_heating = fanPower_heating0 * A_wAH_heat + (k_wAH/100) * fanPower_heating0 * massFilter_cumulative;
				fanPower_cooling = fanPower_cooling0 * A_wAH_cool + (k_wAH/100) * fanPower_cooling0 * massFilter_cumulative;
				
				// Return duct leakage alterations due to filter loading
				retLF = retLF0 * A_DL + (k_DL/100) * retLF0 * massFilter_cumulative;
				
				// Change filter when airflow rate drops to half lowest speed (reset AH airflow, power and duct leakage)
				if((qAH_heat <= (0.5 * qAH_low)) || (qAH_cool <= (0.5 * qAH_low))) {
						qAH_heat = qAH_heat0 * A_qAH_heat;
						qAH_cool = qAH_cool0 * A_qAH_cool;
						fanPower_heating = fanPower_heating0 * A_wAH_heat;
						fanPower_cooling = fanPower_cooling0 * A_wAH_cool;
						retLF = retLF0 * A_DL;
						massFilter_cumulative = 0;
						filterChanges++;
					}
				// Cooling capacity air flow correction term (in CFM)
				qAH_cfm = qAH_cool / .0004719;
				qAHcorr = 1.62 - .62 * qAH_cfm / (400 * capacityraw) + .647 * log(qAH_cfm / (400 * capacityraw));
			}
			// [END] Filter loading calculations =================================================================

			ventSumIN = 0;						// Setting sum of supply mechanical ventilation to zero
			ventSumOUT = 0;						// Setting sum of exhaust mechanical ventilation to zero

			// RIVEC dose and exposure calculations
			turnoverOld = turnover;
			relDoseOld = relDose;

			// Dose and Exposure calculations based on ACH of house rather than just mech vent
			turnoverRealOld = turnoverReal;
			relDoseRealOld = relDoseReal;

			nonRivecVentSumIN = 0;				// Setting sum of non-RIVEC supply mechanical ventilation to zero
			nonRivecVentSumOUT = 0;				// Setting sum of non-RIVEC exhaust mechanical ventilation to zero

			// [START] Read in Weather Data from External Weather File ==============================================================================

			weatherFile >> day >> idirect >> solth >> weatherTemp >> HROUT >> windSpeed >> direction >> pRef >> sc; 

			// Print out simulation day to screen
			if(minute_day == 0) {
				system("CLS");
				cout << "REGCAP++ Building Simulation Tool LBNL" << endl << endl;
				cout << "Batch File: \t " << batchFile_name << endl;
				cout << "Input File: \t " << inPath + input_file << ".csv" << endl;
				cout << "Output File: \t " << outPath + output_file << ".rco" << endl;
				cout << "Weather File: \t " << weather_file << endl << endl;
				
				cout << "Simulation: " << sim + 1 << "/" << numSims << endl << endl;
				cout << "Day = " << day << endl;
			}

			////These are the average temperature for the first day of the year.
			//if(weather_file == "01" && MINUTE == 1) { //Brennan. I went through each cz by hand and calculated the first days average temp
			//	dailyCumulativeTemp = 22.6 * 1440; //this hsould evaluate for minute = 0 and day = 1, and then continue on as normal
			//		} else if(weather_file == "02" && MINUTE == 1){
			//	dailyCumulativeTemp = 7.8 * 1440;
			//		} else if(weather_file == "03" && MINUTE == 1){
			//	dailyCumulativeTemp = 12.9 * 1440;
			//		} else if(weather_file == "04" && MINUTE == 1){
			//	dailyCumulativeTemp = 8.7 * 1440;
			//		} else if(weather_file == "05" && MINUTE == 1){
			//	dailyCumulativeTemp = 10.1 * 1440;
			//		} else if(weather_file == "06" && MINUTE == 1){
			//	dailyCumulativeTemp = 8.7 * 1440;
			//		} else if(weather_file == "07" && MINUTE == 1){
			//	dailyCumulativeTemp = 2.0 * 1440;
			//		} else if(weather_file == "08" && MINUTE == 1){
			//	dailyCumulativeTemp = 6.1 * 1440;
			//		} else if(weather_file == "09" && MINUTE == 1){
			//	dailyCumulativeTemp = -0.9 * 1440;
			//		} else if(weather_file == "10" && MINUTE == 1){
			//	dailyCumulativeTemp = -5.6 * 1440;
			//		} else if(weather_file == "11" && MINUTE == 1){
			//	dailyCumulativeTemp = -3.3 * 1440;
			//		} else if(weather_file == "12" && MINUTE == 1){
			//	dailyCumulativeTemp = 0.5 * 1440;
			//		} else if(weather_file == "13" && MINUTE == 1){
			//	dailyCumulativeTemp = -10.3 * 1440;
			//		} else if(weather_file == "14" && MINUTE == 1){
			//	dailyCumulativeTemp = -6.5 * 1440;
			//		} else if(weather_file == "15" && MINUTE == 1){
			//	dailyCumulativeTemp = -15.1 * 1440;
			//	}

			//if(minute_day == 0) {			// Resetting number of minutes into day every 24 hours
			//	dailyAverageTemp = dailyCumulativeTemp / 1440;
			//	averageTemp.push_back (dailyAverageTemp); //provides the prior day's average temperature...need to do something for day one
			//	dailyCumulativeTemp = 0;			// Resets daily average outdoor temperature to 0 at beginning of new day
			//}
			
			// For 7 day moving average heating/cooling thermostat decision
			if(day > 7 && minute_day == 0) {
				averageTemp.erase(averageTemp.begin());
				runningAverageTemp = 0;
				for(int i = 0; i < 7 ; i++) {
					runningAverageTemp = runningAverageTemp + averageTemp[i];
				}
				runningAverageTemp = runningAverageTemp/7;
			}
			
			//dailyCumulativeTemp = dailyCumulativeTemp + weatherTemp;
			
			pRef = 1000 * pRef;					// Convert reference pressure to [Pa]
			tempOut = 273.15 + weatherTemp;		// Convert outside air temperature to [K]

			sc = sc / 10;						// Converting cloud cover index to decimal fraction
			windSpeed = windSpeed * windSpeedCorrection;		// Correct met wind speed to speed at building eaves height [m/s]

			if(windSpeed < 1)					// Minimum wind velocity allowed is 1 m/s to account for non-zero start up velocity of anenometers
				windSpeed = 1;					// Wind speed is never zero

			for(int k=0; k < 4; k++)			// Wind direction as a compass direction?
				Sw[k] = Swinit[k][int (direction + 1) - 1];		// -1 in order to allocate from array (1 to n) to array (0 to n-1)

			// [END] Read in Weather Data from External Weather File ==============================================================================

			// Fan Schedule Inputs
			// Assumes operation of dryer and kitchen fans, then 1 - 3 bathroom fans
			if(dynamicScheduleFlag == 1) {
				switch (bathroomSchedule) {
				case 1:
					fanschedulefile >> dryerFan >> kitchenFan >> bathOneFan >> bathTwoFan;
					break;
				case 2:
					fanschedulefile >> dryerFan >> kitchenFan >> bathOneFan >> bathTwoFan >> bathThreeFan;
					break;
				case 3:
					fanschedulefile >> dryerFan >> kitchenFan >> bathOneFan >> bathTwoFan >> bathThreeFan;
					break;
				}
			}

			HOUR = int (minute_day / 60);	// HOUR is hours into the current day - used to control diurnal cycles for fans

			if(HOUR == 24)
				HOUR = 0;

			//MINUTE++;
			//MINUTE = MINUTE + dt;				// Is number of minutes into simulation - used for beginning and end of cycle fan operation
			//ttime = int ((minute_day - 1) / 60) + 1;      // Hours into day for thermostat schedule, now using HOUR

			// Day 1 of simulation is a Sunday then weekendFlag = 1 every Saturday and Sunday, equals 0 rest of the time
			if((int (day / 7) == day / 7.) || (int ((day - 1) / 7) == (day - 1) / 7.))
				weekendFlag = 1;
			else
				weekendFlag = 0;

			// [START] Occupancy Schedules ====================================================================================
			
			// Set all occupied[n] = 1 to negate occupancy calculations of dose and exposure
			// Weekends (Sat and Sun)
			if(weekendFlag == 1) {

				occupied[0] = 1;         //Midnight to 01:00
				occupied[1] = 1;         //01:00 to 02:00
				occupied[2] = 1;         //02:00 to 03:00
				occupied[3] = 1;
				occupied[4] = 1;
				occupied[5] = 1;
				occupied[6] = 1;
				occupied[7] = 1;
				occupied[8] = 1;
				occupied[9] = 1;
				occupied[10] = 1;
				occupied[11] = 1;
				occupied[12] = 1;        //Midday to 13:00
				occupied[13] = 1;
				occupied[14] = 1;
				occupied[15] = 1;
				occupied[16] = 1;
				occupied[17] = 1;
				occupied[18] = 1;
				occupied[19] = 1;
				occupied[20] = 1;
				occupied[21] = 1;
				occupied[22] = 1;
				occupied[23] = 1;        //23:00 to Midnight
			}

			// Week days (Mon to Fri). Brennan: iain suggests changing al lthese to "1".
			if(weekendFlag == 0) {
				occupied[0] = 1;         //Midnight to 01:00
				occupied[1] = 1;         //01:00 to 02:00
				occupied[2] = 1;         //02:00 to 03:00
				occupied[3] = 1;
				occupied[4] = 1;
				occupied[5] = 1;
				occupied[6] = 1;
				occupied[7] = 1;
				occupied[8] = 1;		// 0, Brennan, I changed these to 1, per Iain.
				occupied[9] = 1;
				occupied[10] = 1;
				occupied[11] = 1;
				occupied[12] = 1;       //Midday to 13:00
				occupied[13] = 1;
				occupied[14] = 1;
				occupied[15] = 1;		// 0, Brennan, I changed these to 1, per Iain.
				occupied[16] = 1;
				occupied[17] = 1;
				occupied[18] = 1;
				occupied[19] = 1;
				occupied[20] = 1;
				occupied[21] = 1;
				occupied[22] = 1;
				occupied[23] = 1;       //23:00 to Midnight
			}
			// [END] Occupancy Schedules ====================================================================================

			if(MINUTE == 1) {				// Setting initial humidity conditions
				for(int i = 0; i < 5; i++) {
					hrold[i] = HROUT;
					HR[i] = HROUT;
				}
			}

			// Calculate air densities
			double airDensityOUT = airDensityRef * airTempRef / tempOut;		// Outside Air Density
			double airDensityIN = airDensityRef * airTempRef / tempHouse;		// Inside Air Density
			double airDensityATTIC = airDensityRef * airTempRef / tempAttic;	// Attic Air Density
			double airDensitySUP = airDensityRef * airTempRef / tempSupply;		// Supply Duct Air Density
			double airDensityRET = airDensityRef * airTempRef / tempReturn;		// Return Duct Air Density

			// Finding the month for solar radiation calculations
			int month;

			if(day <= 31)
				month = 1;
			if(day > 31 && day <= 59)
				month = 2;
			if(day > 60 && day <= 90)
				month = 3;
			if(day > 91 && day <= 120)
				month = 4;
			if(day > 121 && day <= 151)
				month = 5;
			if(day > 152 && day <= 181)
				month = 6;
			if(day > 182 && day <= 212)
				month = 7;
			if(day > 213 && day <= 243)
				month = 8;
			if(day > 244 && day <= 273)
				month = 9;
			if(day > 274 && day <= 304)
				month = 10;
			if(day > 305 && day <= 334)
				month = 11;
			if(day > 335)
				month = 12;

			int NOONMIN = 720 - minute_day;
			double HA = pi * .25 * NOONMIN / 180;			// Hour Angle
			double L = pi * latitude / 180;					// LATITUDE

			// SOLAR DECLINATION FROM ASHRAE P.27.2, 27.9IP  BASED ON 21ST OF EACH MONTH
			double dec;
			double Csol;

			switch (month) {
			case 1:
				dec = -20 * pi / 180;
				Csol = .103;
				break;
			case 2:
				dec = -10.8 * pi / 180;
				Csol = .104;
				break;
			case 3:
				dec = 0;
				Csol = .109;
				break;
			case 4:
				dec = 11.6 * pi / 180;
				Csol = .12;
				break;
			case 5:
				dec = 20 * pi / 180;
				Csol = .13;
				break;
			case 6:
				dec = 23.45 * pi / 180;
				Csol = .137;
				break;
			case 7:
				dec = 20.6 * pi / 180;
				Csol = .138;
				break;
			case 8:
				dec = 12.3 * pi / 180;
				Csol = .134;
				break;
			case 9:
				dec = 0;
				Csol = .121;
				break;
			case 10:
				dec = -10.5 * pi / 180;
				Csol = .111;
				break;
			case 11:
				dec = -19.8 * pi / 180;
				Csol = .106;
				break;
			case 12:
				dec = -23.45 * pi / 180;
				Csol = .103;
				break;
			}

			double SBETA = cos(L) * cos(dec) * cos(HA) + sin(L) * sin(dec);
			double CBETA = sqrt(1 - pow(SBETA, 2));
			double CTHETA = CBETA;

			// accounting for slight difference in solar measured data from approximate geometry calcualtions
			if(SBETA < 0)
				SBETA = 0;

			double hdirect = SBETA * idirect;
			double diffuse = solth - hdirect;

			// FOR SOUTH ROOF
			double SIGMA = pi * roofPitch / 180;		//ROOF PITCH ANGLE
			CTHETA = CBETA * 1 * sin(SIGMA) + SBETA * cos(SIGMA);
			double ssolrad = idirect * CTHETA + diffuse;

			// FOR NORTH ROOF
			CTHETA = CBETA * -1 * sin(SIGMA) + SBETA * cos(SIGMA);

			if(CTHETA < 0)
				CTHETA = 0;

			double nsolrad = idirect * CTHETA + diffuse;

			if(day == 1)
				hcFlag = 1;					// Start with HEATING (simulations start in January)

			// 7 day outdoor temperature running average. If <= 60F we're heating. If > 60F we're cooling
			if(runningAverageTemp <= (60 - 32) * 5.0 / 9.0)
				hcFlag = 1;					// Heating
			else
				hcFlag = 2;					// Cooling
			
			// Setting thermostat heat/cool mode:
			/*if(tempOld[15] < 295.35 && economizerRan == 0)			// 295.35K = 22.2C = 71.96F
				//if(day <= 152 || day >= 283)							// EISG settings for Oakland/San Fran
				hcFlag = 1;                 // Turn on HEATING
			else
				hcFlag = 2;                 // Turn on COOLING
			*/
			// ====================== HEATING THERMOSTAT CALCULATIONS ============================
			if(hcFlag == 1) {
				qAH = qAH_heat;              // Heating Air Flow Rate [m3/s]
				UA = UAh;					 // Thermal Conductance [W/K]
				rceil = ceilRval_heat;

				if(tempOld[15] > (heatThermostat[HOUR] + .5))
					AHflag = 0;					// Heat off if building air temp above setpoint

				if(AHflag == 0 || AHflag == 100) {
					if(tempOld[15] <= (heatThermostat[HOUR] - .5))
						set = 1;				// Air handler off, and tin below setpoint - 0.5 degrees
					else
						set = 0;				// Air handler off, but tin above setpoint - 0.5 degrees
				}

				if(tempOld[15] < (heatThermostat[HOUR] - .5))
					AHflag = 1;					// turn on air handler/heat

				if(tempOld[15] >= (heatThermostat[HOUR] - .5) && tempOld[15] <= (heatThermostat[HOUR] + .5)) {
					if(set == 1)
						AHflag = 1;
					else
						AHflag = 0;
				}

				if(AHflag == 0 && AHflag != AHflagPrev)
					endrunon = MINUTE + 1;		// Adding 1 minute runon during which heat from beginning of cycle is put into air stream

				if(AHflag == 1)
					hcap = hcapacity / AFUE;	// hcap is gas consumed so needs to divide output (hcapacity) by AFUE

				// ====================== COOLING THERMOSTAT CALCULATIONS ========================
			} else {
				hcap = 0;
				qAH = qAH_cool;						// Cooling Air Flow Rate
				UA = UAc;
				rceil = ceilRval_cool;
				endrunon = 0;
				prerunon = 0;

				if(tempOld[15] < (coolThermostat[HOUR] - .5))
					AHflag = 0;

				if(AHflag == 0 || AHflag == 100) {
					if(tempOld[15] >= (coolThermostat[HOUR] + .5))
						set = 1;				// 1 = AH OFF and house temp below thermostat setpoint - 0.5
					else
						set = 0;				// 0 = AH OFF and house temp above thermostat setpoint - 0.5
				}

				if(tempOld[15] >= (coolThermostat[HOUR] + .5)) {
					AHflag = 2;
				}

				if(tempOld[15] < (coolThermostat[HOUR] + .5) && tempOld[15] >= (coolThermostat[HOUR] - .5)) {
					if(set == 1)
						AHflag = 2;
					else
						AHflag = 0;
				}

				if(AHflag != AHflagPrev && AHflag == 2) {				// First minute of operation
					compTime = 1;
					compTimeCount = 1;
				} else if(AHflag == 2 && compTimeCount == 1) {			// Second minute of operation
					compTime = 2;
					compTimeCount = 0;
				} else if(AHflag == 2 && compTimeCount == 0) {
					compTime = 0;
				} else if(AHflag != AHflagPrev && AHflag == 0) {		// End of cooling cycle
					compTime = 0;
				}

				// [START] ====================== ECONOMIZER RATIONALE ===============================
				econodt = tempHouse - tempOut;			// 3.333K = 6F

				// No cooling, 6F dt (internal/external) and tempHouse > 21C for economizer operation. 294.15 = 21C
					//if(AHflag == 0 && econodt >= 3.333 && tempHouse > 294.15 && economizerUsed == 1) {
				if(AHflag == 0 && econodt >= 3.33 && tempHouse > 294.15 && economizerUsed == 1 && hcFlag == 2) {
						econoFlag = 1;
												
						C = Ceconomizer;
						ceilingC = C * (R + X) / 2;
						AL4 = atticC * sqrt(airDensityRef / 2) * pow(4, (atticPressureExp - .5));

						if(AL4 == 0)
							AL4 = ceilingC * sqrt(airDensityRef / 2) * pow(4, (atticPressureExp - .5));

						AL5 = C * sqrt(airDensityRef / 2) * pow(4, (n - .5));
					} else {
						econoFlag = 0;
					}
			}	// [END] ========================== END ECONOMIZER ====================================



			// [START] RIVEC Decision ==================================================================================================================

			if(nonRivecVentSumIN > nonRivecVentSumOUT)						//ventSum based on largest of inflow or outflow
				nonRivecVentSum = nonRivecVentSumIN;
			else
				nonRivecVentSum = nonRivecVentSumOUT;

			// Choose RIVEC time periods depending on heating or cooling
			switch (hcFlag) {

			case 1:	// HEATING TIMES
				baseStart		= 12;
				baseEnd			= 4;
				peakStart		= 4;
				peakEnd			= 8;
				recoveryStart	= 8;
				recoveryEnd		= 12;
				break;

			case 2: // COOLING TIMES
				baseStart		= 22;
				baseEnd			= 14;
				peakStart		= 14;
				peakEnd			= 18;
				recoveryStart	= 18;
				recoveryEnd		= 22;
				break;
			}

			if (HOUR == peakEnd)
				peakFlag = 1;			// Prevents two peak periods in the same day when there is heating and cooling

			for(int i=0; i < numFans; i++) {


				// [START] ---------------------FAN 50---------- RIVEC OPERATION BASED ON CONTROL ALGORITHM v6
				// v6 of the algorithm only uses the peakStart and peakEnd variables, no more base or recovery periods.(

				if(fan[i].oper == 50 || fan[i].oper == 13 || fan[i].oper == 17) { //traditional (50), cfis (13) or erv+ahu (17) fans for rivec control
 
					if(minute_hour == 1 || minute_hour == 11 || minute_hour == 21 || minute_hour == 31 || minute_hour == 41 || minute_hour == 51) {
						// rivecOn = 1 or 0: 1 = whole-house fan ON, 0 = whole-house fan OFF
						rivecOn = 0;

						//0.012 kg/kg




						  
				  //    	if(HumContType == 2){  				   				
						////Indoor and Outdoor sensor based control. It really seems we will need some sense of the relative parts of the year when these are true...We may need to toy with these relExp thresholds
						//	
						//	if(RHhouse >= 55){ //Engage increased or decreased ventilation only if house RH is >60 (or 55% maybe?). 

						//		if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
						//			if(relExp >= 2.5 || relDose > 1){ //have to with high exp
						//				rivecOn = 1;
						//			} else { //otherwise off
						//				rivecOn = 0;
						//			}
						//		} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
						//			if(relExp >= 0.50 || relDose > 1){ //control to exp = 0.5. Need to change this value based on weighted avg results.
						//				rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}
						//	} else {
						//		if(relExp >= 0.95 || relDose > 1){ 
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	}
						//}

				      	if(HumContType == 1){  	//Cooling system tie-in.			   				
							
							if(hcFlag == 2){ //test if we're in cooling season
								if(AHflag == 2){
									rivecOn = 1;
								} else
									if(relExp >= 2.5 || relDose > 1.0){ //have to with high exp
										rivecOn = 1;
									} else { //otherwise off
										rivecOn = 0;					
									}
							} else //if NOT in cooling season
								if(relExp >= 0.95 || relDose > 1.0){ //have to with high exp
										rivecOn = 1;
									} else { //otherwise off
										rivecOn = 0;					
									}
								}


				      	if(HumContType == 2){  	//Fixed control. 	   				
						//Indoor and Outdoor sensor based control. 
							
							if(RHhouse >= 55){ //Engage increased or decreased ventilation only if house RH is >60 (or 55% maybe?). 

								if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
									if(relExp >= 2.5 || relDose > 1.0){ //have to with high exp
										rivecOn = 1;
									} else { //otherwise off
										rivecOn = 0;
									}
								} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
									if(relExp >= 0.50 || relDose > 1.0){ //control to exp = 0.5. Need to change this value based on weighted avg results.
										rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
									} else {
										rivecOn = 0;
									}
								}
							} else {
								if(relExp >= 0.95 || relDose > 1.0){ 
									rivecOn = 1;
								} else {
									rivecOn = 0;
								}
							}
						}

				      	if(HumContType == 3){  	//Fixed control + cooling system tie-in.		   				
						//Indoor and Outdoor sensor based control. 
							
							if(RHhouse >= 55){ //Engage increased or decreased ventilation only if house RH is >60 (or 55% maybe?). 

								if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
									if(AHflag == 2){
										rivecOn = 1;
									} else
										if(relExp >= 2.5 || relDose > 1.0){ //have to with high exp
											rivecOn = 1;
										} else { //otherwise off
											rivecOn = 0;
										}
								} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
									if(relExp >= 0.50 || relDose > 1.0){ //control to exp = 0.5. Need to change this value based on weighted avg results.
										rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
									} else {
										rivecOn = 0;
									}
								}
							} else {
								if(relExp >= 0.95 || relDose > 1.0){ 
									rivecOn = 1;
								} else {
									rivecOn = 0;
								}
							}
						}



				      	if(HumContType == 4){ //Proportional control.  				   				
						//Indoor and Outdoor sensor based control. 

							if(RHhouse >= 55){

								if(HROUT > HR[3]){ //More humid outside than inside, want to under-vent.  
									relExpTarget = 1 + (2.5-1) * abs((HR[3]-HROUT) / (wDiffMaxNeg)); //wDiffMax has to be an avergaed value, because in a real-world controller you would not know this. 
									if(relExpTarget > 2.5){
										relExpTarget = 2.5;
									}
									if(relExp >= relExpTarget || relDose > 1){ //relDose may be over a 1-week or 2-week time span...
										rivecOn = 1;
									} else { //otherwise off
										rivecOn = 0;
									}
								} else { // More humid inside than outside, want to over-vent
									relExpTarget = 1 - abs((HR[3]- HROUT) / (wDiffMaxPos));
									if(relExpTarget < 0){
										relExpTarget = 0;
									}
									if(relExp >= relExpTarget || relDose > 1){ //
										rivecOn = 1;
									} else {
										rivecOn = 0;
									}
								}
							} else {
								if(relExp >= 0.95 || relDose > 1){ 
									rivecOn = 1;
								} else {
									rivecOn = 0;
								}
							}
						}
						

				      	if(HumContType == 5){ //Proportional control + cooling system tie-in. 				   				
						//Indoor and Outdoor sensor based control. 
							if(RHhouse >= 55){

								if(HROUT > HR[3]){ //More humid outside than inside, want to under-vent.  
									relExpTarget = 1 + (2.5-1) * abs((HR[3]-HROUT) / (wDiffMaxNeg)); //wDiffMax has to be an avergaed value, because in a real-world controller you would not know this. 
									if(relExpTarget > 2.5){
										relExpTarget = 2.5;
									}
									if(AHflag == 2){
										rivecOn = 1;
									} else if(relExp >= relExpTarget || relDose > 1){ //relDose may be over a 1-week or 2-week time span...
											rivecOn = 1;
										} else { //otherwise off
											rivecOn = 0;
										}
									} else { // More humid inside than outside, want to over-vent
										relExpTarget = 1 - abs((HR[3]- HROUT) / (wDiffMaxPos));
										if(relExpTarget < 0){
											relExpTarget = 0;
										}
										if(relExp >= relExpTarget || relDose > 1){ //
											rivecOn = 1;
										} else {
											rivecOn = 0;
										}
									}
							} else {
								if(relExp >= 0.95 || relDose > 1){ 
									rivecOn = 1;
								} else {
									rivecOn = 0;
								}
							}
						}


						if(HumContType == 6){ //Monthly Seasonal Control
						//Monthly timer-based control, based on mean HRdiff by month. doseTargets are based on weighted average targeting dose = 1.5 during low-ventilation months, targeting annual dose of 0.98. 

							if(weather_file == "Orlando"){
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;
							} if(weather_file == "Charleston"){
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;
							} if(weather_file == "07"){ //Baltimore
								FirstCut = 6; 
								SecondCut = 9;
								doseTarget = 0.876;
							} if(weather_file == "01"){ //Miami
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;
							} if(weather_file == "02"){ //Houston
								FirstCut = 4; 
								SecondCut = 10;
								doseTarget = 0.61;
							} if(weather_file == "04"){ //Memphis
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;
							}

							if(month <= FirstCut || month >= SecondCut){ //High ventilation months with net-humidity transport from inside to outside.
								if(relDose > doseTarget){
									rivecOn = 1;
								} else {						
									rivecOn = 0;
								} 
							} else { //Low ventilation months with net-humidity transport from outside to inside.
								if(relExp >= 2.5 || relDose > 1.5){
									rivecOn = 1;
								} else {
									rivecOn = 0;
								}
							}
						}


				  //    	if(HumContType == 7){  	//Fixed control + cooling system tie-in + Monthly Seasonal Control.		   				
						////Indoor and Outdoor sensor based control. 

						//	if(weather_file == "Orlando"){
						//		FirstCut = 4; 
						//		SecondCut = 11;
						//		doseTarget = 0.46;
						//	} if(weather_file == "Charleston"){
						//		FirstCut = 5; 
						//		SecondCut = 10;
						//		doseTarget = 0.72;
						//	} if(weather_file == "07"){ //Baltimore
						//		FirstCut = 6; 
						//		SecondCut = 9;
						//		doseTarget = 0.876;
						//	} if(weather_file == "01"){ //Miami
						//		FirstCut = 4; 
						//		SecondCut = 11;
						//		doseTarget = 0.46;
						//	} if(weather_file == "02"){ //Houston
						//		FirstCut = 4; 
						//		SecondCut = 10;
						//		doseTarget = 0.61;
						//	} if(weather_file == "04"){ //Memphis
						//		FirstCut = 5; 
						//		SecondCut = 10;
						//		doseTarget = 0.72;
						//	}

						//	if(month <= FirstCut || month >= SecondCut){
						//		doseTarget = doseTarget;
						//	} else {
						//		doseTarget = 1.5;
						//	}

						//	if(RHhouse >= 55){ //Engage increased or decreased ventilation only if house RH is >60 (or 55% maybe?). 

						//		if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
						//			if(AHflag == 2){
						//				rivecOn = 1;
						//			} else
						//				if(relExp >= 2.5 || relDose > doseTarget){ //have to with high exp 0.61
						//					rivecOn = 1;
						//				} else { //otherwise off
						//					rivecOn = 0;
						//				}
						//		} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
						//			if(relExp >= 0.5 || relDose > doseTarget){ //control to exp = 0.5. Need to change this value based on weighted avg results.
						//				rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}

						//	} else {
						//		if(relExp >= 0.95 || relDose > doseTarget){
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	}
						//}


				      	if(HumContType == 7){  	//Fixed control + cooling system tie-in + Monthly Seasonal Control.		   				
						//Indoor and Outdoor sensor based control. 

							//if(weather_file == "Orlando"){
							//	doseTarget = 0.46;
							//} if(weather_file == "Charleston"){
							//	doseTarget = 0.72;
							//} if(weather_file == "07"){ //Baltimore
							//	doseTarget = 0.876;
							//} if(weather_file == "01"){ //Miami
							//	doseTarget = 0.46;
							//} if(weather_file == "02"){ //Houston
							//	doseTarget = 0.61;
							//} if(weather_file == "04"){ //Memphis
							//	doseTarget = 0.72;
							//}

							//if(weather_file == "Orlando"){
							//	HiDose = 1.2;
							//	doseTarget = 0.51;
							//} if(weather_file == "Charleston"){
							//	HiDose = 1.3;
							//	doseTarget = 0.47;
							//} if(weather_file == "07"){ //Baltimore
							//	HiDose = 1.4;
							//	doseTarget = 0.66;
							//} if(weather_file == "01"){ //Miami
							//	HiDose = 1.1;
							//	doseTarget = 0.59;
							//} if(weather_file == "02"){ //Houston
							//	HiDose = 1.3;
							//	doseTarget = 0.36;
							//} if(weather_file == "04"){ //Memphis
							//	HiDose = 1.3;
							//	doseTarget = 0.64;
							//}

							if(weather_file == "Orlando"){
								HiDose = 1.2;
								doseTarget = 0.38; 
							} if(weather_file == "Charleston"){
								HiDose = 1.2; 
								doseTarget = 0.47;
							} if(weather_file == "07"){ //Baltimore
								HiDose = 1.3;
								doseTarget = 0.66;
							} if(weather_file == "01"){ //Miami
								HiDose = 1.1;
								doseTarget = 0.38; 
							} if(weather_file == "02"){ //Houston
								HiDose = 1.2; 
								doseTarget = 0.36;
							} if(weather_file == "04"){ //Memphis
								HiDose = 1.2;
								doseTarget = 0.64; 
							}

							if(HROUT > HR[3]){ //do not want to vent. 
								if(AHflag == 2){
									rivecOn = 1;
								} else
									if(relExp >= 2.5 || relDose > HiDose){ 
										rivecOn = 1;
									} else { //otherwise off
										rivecOn = 0;
									}
							} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
								if(relDose > doseTarget){ 
									rivecOn = 1; 
								} else {
									rivecOn = 0;
								}
							}
						}



						if(HumContType == 8){ //Monthly Seasonal controller + time of day
						//Monthly timer-based control, based on mean HRdiff by month. doseTargets are based on weighted average targeting dose = 1.5 during low-ventilation months, targeting annual dose of 0.98. 

							if(weather_file == "Orlando"){
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;
								//PreLows = {;
								//PreHighs = ;
								//SummLows =
								//SummHighs = 
								//PostLows = 
								//PostHighs = 
							} if(weather_file == "Charleston"){
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;
							} if(weather_file == "07"){ //Baltimore
								FirstCut = 6; 
								SecondCut = 9;
								doseTarget = 0.876;
							} if(weather_file == "01"){ //Miami
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;
							} if(weather_file == "02"){ //Houston
								FirstCut = 4; 
								SecondCut = 10;
								doseTarget = 0.61;
							} if(weather_file == "04"){ //Memphis
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;
							}

							if(month <= FirstCut || month >= SecondCut){ //High ventilation months with net-humidity transport from inside to outside.
								if(HOUR >= 3 && HOUR <= 7){ //Maybe change this to the warmest hours of the day.
									rivecOn = 1;
								} else {
									if(relDose > doseTarget){
										rivecOn = 1;
									} else {						
										rivecOn = 0;
									} 
								}
							} else { //Low ventilation months with net-humidity transport from outside to inside.
								if(HOUR >= 8 && HOUR <= 11){
									rivecOn = 0;
								} if(HOUR >= 15 && HOUR <=18){
									rivecOn = 1;
								} else {
									if(relExp >= 2.5 || relDose > 1.5){
										rivecOn = 1;
									} else {
										rivecOn = 0;
									}
								}
							}
						}

				  //    	if(HumContType == 9){  	//Fixed control + Monthly Seasonal Control.		   				
						////Indoor and Outdoor sensor based control. 

						//	if(weather_file == "Orlando"){
						//		FirstCut = 4; 
						//		SecondCut = 11;
						//		doseTarget = 0.46;
						//	} if(weather_file == "Charleston"){
						//		FirstCut = 5; 
						//		SecondCut = 10;
						//		doseTarget = 0.72;
						//	} if(weather_file == "07"){ //Baltimore
						//		FirstCut = 6; 
						//		SecondCut = 9;
						//		doseTarget = 0.876;
						//	} if(weather_file == "01"){ //Miami
						//		FirstCut = 4; 
						//		SecondCut = 11;
						//		doseTarget = 0.46;
						//	} if(weather_file == "02"){ //Houston
						//		FirstCut = 4; 
						//		SecondCut = 10;
						//		doseTarget = 0.61;
						//	} if(weather_file == "04"){ //Memphis
						//		FirstCut = 5; 
						//		SecondCut = 10;
						//		doseTarget = 0.72;
						//	}

						//	if(month <= FirstCut || month >= SecondCut){
						//		doseTarget = doseTarget;
						//	} else {
						//		doseTarget = 1.5;
						//	}

						//	if(RHhouse >= 55){ //Engage increased or decreased ventilation only if house RH is >60 (or 55% maybe?).

						//		if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
						//			if(relExp >= 2.5 || relDose > doseTarget){ //have to with high exp 0.61
						//				rivecOn = 1;
						//			} else { //otherwise off
						//				rivecOn = 0;
						//			}
						//		} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
						//			if(relExp >= 0.50 || relDose > doseTarget){ //control to exp = 0.5. Need to change this value based on weighted avg results.
						//				rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}
						//	} else {
						//		if(relExp >= 0.95 || relDose > doseTarget){
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	}
						//}

				      	if(HumContType == 9){  	//Fixed control + Monthly Seasonal Control.		   				
						//Indoor and Outdoor sensor based control. 

							//if(weather_file == "Orlando"){
							//	doseTarget = 0.46;
							//} if(weather_file == "Charleston"){
							//	doseTarget = 0.72;
							//} if(weather_file == "07"){ //Baltimore
							//	doseTarget = 0.876;
							//} if(weather_file == "01"){ //Miami
							//	doseTarget = 0.46;
							//} if(weather_file == "02"){ //Houston
							//	doseTarget = 0.61;
							//} if(weather_file == "04"){ //Memphis
							//	doseTarget = 0.72;
							//}

							//if(weather_file == "Orlando"){
							//	HiDose = 1.2;
							//	doseTarget = 0.51; //1.3 and 0.38
							//} if(weather_file == "Charleston"){
							//	HiDose = 1.3; //try 1.4
							//	doseTarget = 0.47;
							//} if(weather_file == "07"){ //Baltimore
							//	HiDose = 1.4;
							//	doseTarget = 0.66; //1.3 and 0.75
							//} if(weather_file == "01"){ //Miami
							//	HiDose = 1.1;
							//	doseTarget = 0.59; //Consider trying 1.2 and 0.38
							//} if(weather_file == "02"){ //Houston
							//	HiDose = 1.3; //try 1.4
							//	doseTarget = 0.36;
							//} if(weather_file == "04"){ //Memphis
							//	HiDose = 1.3;
							//	doseTarget = 0.64; //1.4 and 0.52
							//}

							//if(weather_file == "Orlando"){
							//	HiDose = 1.2; //1.1
							//	doseTarget = 0.38; 
							//} if(weather_file == "Charleston"){
							//	HiDose = 1.2; //1.15
							//	doseTarget = 0.47;
							//} if(weather_file == "07"){ //Baltimore
							//	HiDose = 1.3; //1.25
							//	doseTarget = 0.66;
							//} if(weather_file == "01"){ //Miami
							//	HiDose = 1.1; //1.05
							//	doseTarget = 0.38; 
							//} if(weather_file == "02"){ //Houston
							//	HiDose = 1.2; //1.15
							//	doseTarget = 0.36;
							//} if(weather_file == "04"){ //Memphis
							//	HiDose = 1.2;
							//	doseTarget = 0.64; 
							//}

							if(weather_file == "Orlando"){
								HiDose = 1.1; //1.1
								doseTarget = 0.38; 
							} if(weather_file == "Charleston"){
								HiDose = 1.15; //1.15
								doseTarget = 0.47;
							} if(weather_file == "07"){ //Baltimore
								HiDose = 1.25; //1.25
								doseTarget = 0.66;
							} if(weather_file == "01"){ //Miami
								HiDose = 1.05; //1.05
								doseTarget = 0.38; 
							} if(weather_file == "02"){ //Houston
								HiDose = 1.15; //1.15
								doseTarget = 0.36;
							} if(weather_file == "04"){ //Memphis
								HiDose = 1.2;
								doseTarget = 0.64; 
							}


							if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
								if(relExp >= 2.5 || relDose > HiDose){ //have to with high exp 0.61
									rivecOn = 1;
								} else { //otherwise off
									rivecOn = 0;
								}
							} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
								if(relDose > doseTarget){ //control to exp = 0.5. Need to change this value based on weighted avg results.
									rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
								} else {
									rivecOn = 0;
								}
							}
						}



						if(HumContType == 10){ //The real opportunities for control based on outside are when the outside value is changing rapidly. Sharp increases, decrease ventilation. Sharp decreases, increase ventilation. 
						//Need to undervent at above the 75th percentile and overvent below the 25th percentile based on a per month basis. 
							
							//Outdoor-only sensor based control


							if(HROUT > 0.012){ //If humid outside, reduce ventilation. 

								if(relExp >= 2.5 || relDose > 1){
									rivecOn = 1; 
								} else {
									rivecOn = 0;
								}

							} else { //If dry outside, increase vnetilation.
								if(relExp >= 0.95 || relDose > 1){ //but we do if exp is high
									rivecOn = 1;
								} else {
									rivecOn = 0; //otherwise don't vent under high humidity condition.
								}
							}
						}


						if(HumContType == 11){ //Monthly Advanced Seasonal Control
						//Monthly timer-based control, based on mean HRdiff by month. doseTargets are based on weighted average targeting dose = 1.5 during low-ventilation months, targeting annual dose of 0.98. 

							if(weather_file == "Orlando"){
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;

								HiMonths[0] = 2;
								HiMonths[1] = 11;
								HiMonths[2] = 12;
								HiMonthDose = 0.38;

								LowMonths[0] = 10;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 1.74;

							} if(weather_file == "Charleston"){
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;

								HiMonths[0] = 10;
								HiMonths[1] = 11;
								HiMonths[2] = 0;
								HiMonthDose = 0.47;

								LowMonths[0] = 8;
								LowMonths[1] = 9;
								LowMonths[2] = 0;
								LowMonthDose = 1.75;

							} if(weather_file == "07"){ //Baltimore
								FirstCut = 6; 
								SecondCut = 9;
								doseTarget = 0.876;

								HiMonths[0] = 10;
								HiMonths[1] = 0;
								HiMonths[2] = 0;
								HiMonthDose = 0.626;

								LowMonths[0] = 7;
								LowMonths[1] = 8;
								LowMonths[2] = 0;
								LowMonthDose = 1.625;

							} if(weather_file == "01"){ //Miami
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;

								HiMonths[0] = 1;
								HiMonths[1] = 2;
								HiMonths[2] = 12;
								HiMonthDose = 0.38;

								LowMonths[0] = 10;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 1.74;

							} if(weather_file == "02"){ //Houston
								FirstCut = 4; 
								SecondCut = 10;
								doseTarget = 0.61;

								HiMonths[0] = 4;
								HiMonths[1] = 10;
								HiMonths[2] = 11;
								HiMonthDose = 0.38;

								LowMonths[0] = 5;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 2.184;

							} if(weather_file == "04"){ //Memphis
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;

								HiMonths[0] = 4;
								HiMonths[1] = 10;
								HiMonths[2] = 11;
								HiMonthDose = 0.492;

								LowMonths[0] = 9;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 2.184;
							}

							//Setting the appropriate Dose Target based on the month
							if(month == HiMonths[0] || month == HiMonths[1]  || month == HiMonths[2]){
								doseTarget = HiMonthDose;
							} else if(month == LowMonths[0] || month == LowMonths[1]  || month == LowMonths[2]){
								doseTarget = LowMonthDose;
							} else if(month > FirstCut && month < SecondCut){
								doseTarget = 1.5;
							} else {
								doseTarget = doseTarget;
							}
							
							if(doseTarget >= 1.5){
								if(relExp >= 2.5 || relDose > doseTarget){
									rivecOn = 1; 
								} else {
									rivecOn = 0;
								}
							}
							else {
								if(relDose > doseTarget){
									rivecOn = 1;
								} else {						
									rivecOn = 0;
								} 
							}
						}


				      	if(HumContType == 12){  	//Monthly Seasonal Control + Cooling system tie-in.		   				
						//Indoor and Outdoor sensor based control. 

							if(weather_file == "Orlando"){
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;
							} if(weather_file == "Charleston"){
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;
							} if(weather_file == "07"){ //Baltimore
								FirstCut = 6; 
								SecondCut = 9;
								doseTarget = 0.876;
							} if(weather_file == "01"){ //Miami
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;
							} if(weather_file == "02"){ //Houston
								FirstCut = 4; 
								SecondCut = 10;
								doseTarget = 0.61;
							} if(weather_file == "04"){ //Memphis
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;
							}

							if(month <= FirstCut || month >= SecondCut){
								doseTarget = doseTarget;
							} else {
								doseTarget = 1.5;
							}

							if(AHflag == 2){
									rivecOn = 1;
							} else if(doseTarget >= 1.5){
								if(relExp >= 2.5 || relDose > doseTarget){
									rivecOn = 1; 
								} else {
									rivecOn = 0;
								}
							} else {
								if(relDose > doseTarget){
									rivecOn = 1;
								} else {						
									rivecOn = 0;
								} 
							}
						}


						if(HumContType == 13){ 
							//Outdoor-only sensor based control, with variable dose targets

							if(weather_file == "Orlando"){
								wCutoff = 0.012;
							} if(weather_file == "Charleston"){
								wCutoff = 0.011;
							} if(weather_file == "07"){ //Baltimore
								wCutoff = 0.006;
							} if(weather_file == "01"){ //Miami
								wCutoff = 0.016;
							} if(weather_file == "02"){ //Houston
								wCutoff = 0.012;
							} if(weather_file == "04"){ //Memphis
								wCutoff = 0.009;
							}

							if(HROUT >= wCutoff){ //If humid outside, reduce ventilation. 
								if(relExp >= 2.5 || relDose > 1.5){
									rivecOn = 1; 
								} else {
									rivecOn = 0;
								}
							} else { //If dry outside, increase vnetilation.
								if(relDose > 0.5){ //but we do if exp is high
									rivecOn = 1;
								} else {
									rivecOn = 0; //otherwise don't vent under high humidity condition.
								}
							}
						}


						if(HumContType == 14){ 
						//Outdoor-only sensor based control, control based on 25th and 75th percentile monthly values for each month and climate zone.

						switch (month) {
						case 1:
							W75 = W75_1;
							W25 = W25_1;
							break;
						case 2:
							W75 = W75_2;
							W25 = W25_2;
							break;
						case 3:
							W75 = W75_3;
							W25 = W25_3;
							break;
						case 4:
							W75 = W75_4;
							W25 = W25_4;
							break;
						case 5:
							W75 = W75_5;
							W25 = W25_5;
							break;
						case 6:
							W75 = W75_6;
							W25 = W25_6;
							break;
						case 7:
							W75 = W75_7;
							W25 = W25_7;
							break;
						case 8:
							W75 = W75_8;
							W25 = W25_8;
							break;
						case 9:
							W75 = W75_9;
							W25 = W25_9;
							break;
						case 10:
							W75 = W75_10;
							W25 = W25_10;
							break;
						case 11:
							W75 = W75_11;
							W25 = W25_11;
							break;
						case 12:
							W75 = W75_12;
							W25 = W25_12;
							break;
						}


							if(HROUT >= W75){ //If humid outside, reduce ventilation. 
								if(relExp >= 2.5 || relDose > 1.5){
									rivecOn = 1; 
								} else {
									rivecOn = 0;
								}
							} else if (HROUT <= W25){ //If dry outside, increase vnetilation.
								if(relDose > 0.5){ //but we do if exp is high
									rivecOn = 1;
								} else {
									rivecOn = 0; //otherwise don't vent under high humidity condition.
								}
							} else {
								if(relExp >= 0.95 || relDose > 1.0){ //Need to reduce this target to make equivalence work out...
									rivecOn = 1;
								} else {
									rivecOn = 0;
								}
							}
						}


					if(HumContType == 15){ //Monthly Advanced Seasonal Control + Fixed Control + Cooling System Tie_in
						//Monthly timer-based control, based on mean HRdiff by month. doseTargets are based on weighted average targeting dose = 1.5 during low-ventilation months, targeting annual dose of 0.98. 

							if(weather_file == "Orlando"){
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;

								HiMonths[0] = 2;
								HiMonths[1] = 11;
								HiMonths[2] = 12;
								HiMonthDose = 0.38;

								LowMonths[0] = 10;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 1.74;

							} if(weather_file == "Charleston"){
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;

								HiMonths[0] = 10;
								HiMonths[1] = 11;
								HiMonths[2] = 0;
								HiMonthDose = 0.47;

								LowMonths[0] = 8;
								LowMonths[1] = 9;
								LowMonths[2] = 0;
								LowMonthDose = 1.75;

							} if(weather_file == "07"){ //Baltimore
								FirstCut = 6; 
								SecondCut = 9;
								doseTarget = 0.876;

								HiMonths[0] = 10;
								HiMonths[1] = 0;
								HiMonths[2] = 0;
								HiMonthDose = 0.626;

								LowMonths[0] = 7;
								LowMonths[1] = 8;
								LowMonths[2] = 0;
								LowMonthDose = 1.625;

							} if(weather_file == "01"){ //Miami
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;

								HiMonths[0] = 1;
								HiMonths[1] = 2;
								HiMonths[2] = 12;
								HiMonthDose = 0.38;

								LowMonths[0] = 10;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 1.74;

							} if(weather_file == "02"){ //Houston
								FirstCut = 4; 
								SecondCut = 10;
								doseTarget = 0.61;

								HiMonths[0] = 4;
								HiMonths[1] = 10;
								HiMonths[2] = 11;
								HiMonthDose = 0.38;

								LowMonths[0] = 5;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 2.184;

							} if(weather_file == "04"){ //Memphis
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;

								HiMonths[0] = 4;
								HiMonths[1] = 10;
								HiMonths[2] = 11;
								HiMonthDose = 0.492;

								LowMonths[0] = 9;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 2.184;
							}

							//Setting the appropriate Dose Target based on the month
							if(month == HiMonths[0] || month == HiMonths[1]  || month == HiMonths[2] || month == LowMonths[0] || month == LowMonths[1]  || month == LowMonths[2]){
								//doseTarget = HiMonthDose;
								if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
									if(AHflag == 2){
										rivecOn = 1;
									} else
										if(relExp >= 2.5 || relDose > LowMonthDose){ //have to with high exp
											rivecOn = 1;
										} else { //otherwise off
											rivecOn = 0;
										}
								} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
									if(relDose > HiMonthDose){ //control to exp = 0.5. Need to change this value based on weighted avg results.
										rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
									} else {
										rivecOn = 0;
									}
								}

							} else {
								if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
									if(AHflag == 2){
										rivecOn = 1;
									} else
										if(relExp >= 2.5 || relDose > 1.5){ //have to with high exp
											rivecOn = 1;
										} else { //otherwise off
											rivecOn = 0;
										}
								} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
									if(relDose > doseTarget){ //control to exp = 0.5. Need to change this value based on weighted avg results.
										rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
									} else {
										rivecOn = 0;
									}
								}
							}
						}



					if(HumContType == 16){ //Monthly Advanced Seasonal Control + Fixed Control
						//Monthly timer-based control, based on mean HRdiff by month. doseTargets are based on weighted average targeting dose = 1.5 during low-ventilation months, targeting annual dose of 0.98. 

							if(weather_file == "Orlando"){
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;

								HiMonths[0] = 2;
								HiMonths[1] = 11;
								HiMonths[2] = 12;
								HiMonthDose = 0.38;

								LowMonths[0] = 10;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 1.74;

							} if(weather_file == "Charleston"){
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;

								HiMonths[0] = 10;
								HiMonths[1] = 11;
								HiMonths[2] = 0;
								HiMonthDose = 0.47;

								LowMonths[0] = 8;
								LowMonths[1] = 9;
								LowMonths[2] = 0;
								LowMonthDose = 1.75;

							} if(weather_file == "07"){ //Baltimore
								FirstCut = 6; 
								SecondCut = 9;
								doseTarget = 0.876;

								HiMonths[0] = 10;
								HiMonths[1] = 0;
								HiMonths[2] = 0;
								HiMonthDose = 0.626;

								LowMonths[0] = 7;
								LowMonths[1] = 8;
								LowMonths[2] = 0;
								LowMonthDose = 1.625;

							} if(weather_file == "01"){ //Miami
								FirstCut = 4; 
								SecondCut = 11;
								doseTarget = 0.46;

								HiMonths[0] = 1;
								HiMonths[1] = 2;
								HiMonths[2] = 12;
								HiMonthDose = 0.38;

								LowMonths[0] = 10;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 1.74;

							} if(weather_file == "02"){ //Houston
								FirstCut = 4; 
								SecondCut = 10;
								doseTarget = 0.61;

								HiMonths[0] = 4;
								HiMonths[1] = 10;
								HiMonths[2] = 11;
								HiMonthDose = 0.38;

								LowMonths[0] = 5;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 2.184;

							} if(weather_file == "04"){ //Memphis
								FirstCut = 5; 
								SecondCut = 10;
								doseTarget = 0.72;

								HiMonths[0] = 4;
								HiMonths[1] = 10;
								HiMonths[2] = 11;
								HiMonthDose = 0.492;

								LowMonths[0] = 9;
								LowMonths[1] = 0;
								LowMonths[2] = 0;
								LowMonthDose = 2.184;
							}

							//Setting the appropriate Dose Target based on the month
							if(month == HiMonths[0] || month == HiMonths[1]  || month == HiMonths[2] || month == LowMonths[0] || month == LowMonths[1]  || month == LowMonths[2]){
								//doseTarget = HiMonthDose;
								if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
									if(relExp >= 2.5 || relDose > LowMonthDose){ //have to with high exp
											rivecOn = 1;
										} else { //otherwise off
											rivecOn = 0;
										}
								} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
									if(relDose > HiMonthDose){ //control to exp = 0.5. Need to change this value based on weighted avg results.
										rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
									} else {
										rivecOn = 0;
									}
								}

							} else {
								if(HROUT > HR[3]){ //do not want to vent. Add some "by what amount" deadband value. 
									if(relExp >= 2.5 || relDose > 1.5){ //have to with high exp
											rivecOn = 1;
										} else { //otherwise off
											rivecOn = 0;
										}
								} else { //want to vent due to high indoor humidity, so maybe we just let it run, without relExp control?
									if(relDose > doseTarget){ //control to exp = 0.5. Need to change this value based on weighted avg results.
										rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
									} else {
										rivecOn = 0;
									}
								}
							}
						}



				  //    	if(HumContType == 4){  				   				
						////Indoor and Outdoor sensor based control. It really seems we will need some sense of the relative parts of the year when these are true...We may need to toy with these relExp thresholds

						//	if(RHhouse >= 55){

						//		if(AHflag == 2){
						//			rivecOn = 1;

						//		} else if((abs(airDensityOUT * fan[i].q * HROUT)) > (abs(airDensityIN * fan[i].q * HR[3]) + latentLoad)){ //TRUE when net-humidity transport is from outside to inside. Reduce ventilation.
						//			relExpTarget = 1 + (2.5-1) * abs((HR[3]-HROUT) / (wDiffMaxNeg)); //wDiffMax has to be an avergaed value, because in a real-world controller you would not know this. 
						//			if(relExpTarget > 2.5){
						//				relExpTarget = 2.5;
						//			}
						//			if(relExp >= relExpTarget || relDose > 1.5){ //relDose may be over a 1-week or 2-week time span...Need to establish an alternate dose target to allow more under-venting during summmer.
						//				rivecOn = 1;
						//			} else { //otherwise off
						//				rivecOn = 0;
						//			}

						//		} else { //TRUE when net-humidity transport is from inside to outside. Increase ventilation. 
						//			relExpTarget = 1 - abs((HR[3]- HROUT) / (wDiffMaxPos));
						//			if(relExpTarget < 0){
						//				relExpTarget = 0;
						//			}
						//			if(relExp >= relExpTarget){ //|| relDose > 1 Brennan removed, because we don't want to limit over-venting to control humidity.
						//				rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}

						//	} else {//I only want this to over-vent during summer. Lock this part of the control out based on cooling thermostat months, maybe? Or remove this entirely. 
						//		if(relExp >= 0.95 || relDose > 1.0){ //Need to reduce this target to make equivalence work out...
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;

						//		}
						//	}
						//}

				  //    	if(HumContType == 4){  				   				
						////Indoor and Outdoor sensor based control. It really seems we will need some sense of the relative parts of the year when these are true...We may need to toy with these relExp thresholds

						//	//if(RHhouse >= 55){
						//	if(weather_file == "Orlando"){
						//		doseTarget = 0.6; //Was 0.7 based on weighted avg calcs
						//	} if(weather_file == "Charleston"){
						//		doseTarget = 0.73; //Was 0.83 based on weighted avg calcs.
						//	} if(weather_file == "07"){
						//		doseTarget = 0.97;
						//	}

						//	//doseTarget = 1;
						//		
						//	//}
						//		 if((abs(airDensityOUT * fan[i].q * HROUT)) > (abs(airDensityIN * fan[i].q * HR[3]) + latentLoad)){ //TRUE when net-humidity transport is from outside to inside. Reduce ventilation.
						//			relExpTarget = 1 + (2.5-1) * abs((HR[3]-HROUT) / (wDiffMaxNeg)); //wDiffMax has to be an avergaed value, because in a real-world controller you would not know this. 
						//			if(relExpTarget > 2.5){
						//				relExpTarget = 2.5;
						//			//} if(AHflag == 2){ //Add some only first 20 minutes of cycle code. 
						//			//	rivecOn = 1;
						//			} if(relExp >= relExpTarget || relDose > 1.5){ //relDose may be over a 1-week or 2-week time span...Need to establish an alternate dose target to allow more under-venting during summmer.
						//				rivecOn = 1;
						//			} else { //otherwise off
						//				rivecOn = 0;
						//			}

						//		} else { //TRUE when net-humidity transport is from inside to outside. Increase ventilation. 
						//			relExpTarget = 1 - abs((HR[3]- HROUT) / (wDiffMaxPos));
						//			if(relExpTarget < 0){
						//				relExpTarget = 0;
						//			} if(relExp >= relExpTarget || relDose > doseTarget){ //|| relDose > 1 Brennan removed, because we don't want to limit over-venting to control humidity. 0.7 for Orlando. 0.83 for Charleston. 0.97 for Baltimore. 
						//				rivecOn = 1; //OR we can change the does calculation based on expected periods of contol function (i.e., 1-week,1-month, etc.)
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}
						//	}

							//} else {//I only want this to over-vent during summer. Lock this part of the control out based on cooling thermostat months, maybe? Or remove this entirely. 
							//	if(relExp >= 0.95 || relDose > 1.0){ //Need to reduce this target to make equivalence work out...
							//		rivecOn = 1;
							//	} else {
							//		rivecOn = 0;

							/*	}
							}
						}*/


						//if(HumContType == 5){ //Marginal improvements, ~2-3% reductions in RH60 and RH70 exceedances.
						////Monthly timer-based control. Can control to exp=2.5 and target ~0.28, or exp=2.0 and target ~0.5

						//	if(month <= 4 || month > 8){ //Jan-April and Sept-Dec, over ventilate. Can experiment with this at 5 and 9, as well. 
						//		if(relExp >= 0.5 || relDose > 0.5){
						//			rivecOn = 1;
						//		} else {						
						//			rivecOn = 0;
						//		} 
						//	} else { //May-Aug, underventilate.
						//		if(relExp >= 2.0 || relDose > 2.0){
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	}
						//}

						//if(HumContType == 5){ //Marginal improvements, ~2-3% reductions in RH60 and RH70 exceedances.
						////Monthly timer-based control. Can control to exp=2.5 and target ~0.28, or exp=2.0 and target ~0.5

						//	if(weather_file == "Orlando"){
						//		FirstCut = 4; 
						//		SecondCut = 10;
						//	} if(weather_file == "Charleston"){
						//		FirstCut = 4; 
						//		SecondCut = 9;
						//	} if(weather_file == "07"){ //Baltimore
						//		FirstCut = 5; 
						//		SecondCut = 8;
						//	} if(weather_file == "01"){ //Miami
						//		FirstCut = 3; 
						//		SecondCut = 11;
						//	} if(weather_file == "02"){ //Houston
						//		FirstCut = 4; 
						//		SecondCut = 9;
						//	} if(weather_file == "04"){ //Memphis
						//		FirstCut = 5; 
						//		SecondCut = 9;
						//	}

						//	if(month <= FirstCut || month > SecondCut){ //Jan-April and Sept-Dec, over ventilate. Can experiment with this at 5 and 9, as well. 
						//		if(relExp >= 0.5 || relDose > 0.5){
						//			rivecOn = 1;
						//		} else {						
						//			rivecOn = 0;
						//		} 
						//	} else { //May-Aug, underventilate.
						//		if(relExp >= 2.0 || relDose > 2.0){
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	}
						//}


						//if(HumContType == 6){ //Marginal improvements, ~2-3% reductions in RH60 and RH70 exceedances.
						////4am to 10am fan control, reduce ventilation between 4 and 10 am. 6-hour control.
						//	if(month < 4 || month > 11){ //control normally Jan-March and Dec.
						//		if(relExp >= 0.95 || relDose > 1){ 
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	} else {						
						//		if(HOUR < 4 || HOUR > 9){ //before 4am or after 10am operate fan controlled to exp < 1
						//			if(relExp >= 0.95 || relDose > 1){
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		} else { //Between 4 and 10 am, only ventilate if exp >2.5
						//			if(relExp >= 2.5){
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}
						//	}
						//}

						//if(HumContType == 7){ //So far, this control makes humidity slightly worse in Med Med home sizes/occ densities.
						////4am to 10am fan control, increase ventilation between 4 and 10am. 6-hour control.
						//	if(month < 4 || month > 11){ //control normally Jan-March and Dec.
						//		if(relExp >= 0.95 || relDose > 1){ 
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	} else {
						//		if(HOUR < 4 || HOUR > 9){ //before 4am or after 10am, under-vent to 1.2
						//			if(relExp >= 1.2){
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		} else { //Between 4 and 10 am, over-vent down to exp 1
						//			if(relExp >= 0.95 || relDose > 1){ 
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}
						//	}
						//}

						//if(HumContType == 8){ //So far, this control makes humidity slightly worse in Med Med home sizes/occ densities.
						////12pm to 6pm fan control, reduce ventilation between 12 and 6. 6-hour control.
						//	if(month < 4 || month > 11){ //control normally Jan-March and Dec.
						//		if(relExp >= 0.95 || relDose > 1){ 
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	} else {
						//		if(HOUR < 12 || HOUR > 17){ //before 12pm or after 6pm operate fan controlled to exp < 1
						//			if(relExp >= 0.95 || relDose > 1){
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		} else { //Between 12 and 6pm, only ventilate if exp >2.5
						//			if(relExp >= 2.5){
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}
						//	}
						//}

						//if(HumContType == 9){ //Marginal improvements, ~2-3% reductions in RH60 and RH70 exceedances.
						////12pm to 6pm fan control, increase ventilation between 12 and 6. 6-hour control.
						//	if(month < 4 || month > 11){ //control normally Jan-March and Dec.
						//		if(relExp >= 0.95 || relDose > 1){ 
						//			rivecOn = 1;
						//		} else {
						//			rivecOn = 0;
						//		}
						//	} else { //All other months, use hourly controls. 
						//		if(HOUR < 12 || HOUR > 17){ //before 12pm or after 6pm, under-vent to exp 1.2
						//			if(relExp >= 1.2){
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		} else { //Between 12 and 6pm, over-vent down to dose 1
						//			if(relExp >= 0.95 || relDose > 1){
						//				rivecOn = 1;
						//			} else {
						//				rivecOn = 0;
						//			}
						//		}
						//	}
						//}

						if(HumContType == 0){
						//Rivec control of number 50 fan type, algorithm v6					     
					
							if(occupied[HOUR]) {				            	// Base occupied
								if(relExp >= 0.95 || relDose >= 1.0)
									rivecOn = 1;

							} else {						                	// Base unoccupied
								if(relExp >= expLimit)
									rivecOn = 1;
							}

							if(HOUR >= peakStart && HOUR < peakEnd && peakFlag == 0) {		// PEAK Time Period
								rivecOn = 0;												// Always off
								if(relExp >= expLimit)
									rivecOn = 1;
							}
						}
					

					//// RIVEC fan operation after algorithm decision
					//if(rivecOn) {	  												// RIVEC has turned ON this fan
					//	fan[i].on = 1;
					//	mechVentPower = mechVentPower + fan[i].power;				// vent fan power
					//	if(fan[i].q > 0) { 											// supply fan - its heat needs to be added to the internal gains of the house
					//		fanHeat = fan[i].power * .84;							// 16% efficiency for this fan
					//		ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
					//	} else { 													// exhaust fan
					//		ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					//	}
					//	rivecMinutes++;
					//}
					//else {
					//	fan[i].on = 0;

					//}
					}
				}
			}
			
				// [END] ========================== END RIVEC Decision ====================================



				
			AHflagPrev = AHflag;
			if(AHflag != 0)
				AHminutes++;			// counting number of air handler operation minutes in the hour

			// the following air flows depend on if we are heating or cooling
			supVelAH = qAH / (pow(supDiameter,2) * pi / 4);
			retVelAH = qAH / (pow(retDiameter,2) * pi / 4);
			qSupReg = -qAH * supLF + qAH;
			qRetReg = qAH * retLF - qAH;
			qRetLeak = -qAH * retLF;
			qSupLeak = qAH * supLF;
			mSupLeak1 = qSupLeak * airDensityIN;
			mRetLeak1 = qRetLeak * airDensityIN;
			mSupReg1 = qSupReg * airDensityIN;
			mRetReg1 = qRetReg * airDensityIN;
			mAH1 = qAH * airDensityIN;			// Initial mass flow through air handler
			mFanCycler = 0;
			mechVentPower = 0;					// Total vent fan power consumption

			if(AHflag == 0) {			// AH OFF
				// the 0 at the end of these terms means that the AH is off
				if(MINUTE >= endrunon) {  // endrunon is the end of the heating cycle + 1 minutes
					mSupReg = 0;
					mAH = 0;
					mRetLeak = 0;
					mSupLeak = 0;
					mRetReg = 0;
					supVel = abs(mSupAHoff) / airDensitySUP / (pow(supDiameter,2) * pi / 4);
					retVel = abs(mRetAHoff) / airDensityRET / (pow(retDiameter,2) * pi / 4);
					AHfanPower = 0;		// Fan power consumption [W]
					AHfanHeat = 0;		// Fan heat into air stream [W]
					hcap = 0;			// Gas burned by furnace NOT heat output (that is hcapacity)
				} else {
					AHflag = 102;		// Note: need to have capacity changed to one quarter of burner capacity during cool down
					mAH = mAH1;
					mRetLeak = mRetLeak1;
					mSupLeak = mSupLeak1;
					mRetReg = mRetReg1;
					mSupReg = mSupReg1;
					supVel = supVelAH;
					retVel = retVelAH;
					AHfanHeat = fanPower_heating * 0.85;	//0.85		// Heating fan power multiplied by an efficiency
					AHfanPower = fanPower_heating;
					hcap = hcapacity / AFUE;

					for(int i = 0; i < numFans; i++) {     // For outside air into return cycle operation
						if(fan[i].oper == 6 && AHminutes <= 20) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 10) {			// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 14) {			// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 18) {			// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 15 && AHminutes <= 20) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 13) {
							if((rivecFlag == 0 && AHminutes <= 20) || (rivecFlag == 1 && rivecOn == 1)){ //This allows RIVEC to operate this fan until it reaches its Exposure setpoint?
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
							ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumIN = nonRivecVentSumIN + abs(fan[i].q) * 3600 / houseVolume;
							}
						}
						if(fan[i].oper == 22 && AHminutes <= 20 && econoFlag == 0) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
					}
				}
			} else {
				if(AHflag == 2) {	//	cooling 
					hcap = 0;
					mAH = mAH1;
					mRetLeak = mRetLeak1;
					mSupLeak = mSupLeak1;
					mRetReg = mRetReg1;
					mSupReg = mSupReg1;
					supVel = supVelAH;
					retVel = retVelAH;
					AHfanHeat = fanPower_cooling * 0.85;	//0.85		// Cooling fan power multiplied by an efficiency (15% efficient fan)
					AHfanPower = fanPower_cooling;
					for(int i = 0; i < numFans; i++) {			// for outside air into return cycle operation
						if(fan[i].oper == 6 && AHminutes <= 20) {
							mFanCycler = fan[i].q * airDensityIN * qAH_cool / qAH_heat;	// correcting this return intake flow so it remains a constant fraction of air handler flow
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 10) {				// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 14) {				// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 18) {				// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 15 && AHminutes <= 20) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 13) {
							if((rivecFlag == 0 && AHminutes <= 20) || (rivecFlag == 1 && rivecOn == 1)){ //This allows RIVEC to operate this fan until it reaches its Exposure setpoint?
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
							ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumIN = nonRivecVentSumIN + abs(fan[i].q) * 3600 / houseVolume;
							}
						}
						if(fan[i].oper == 22 && AHminutes <= 20 && econoFlag == 0) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
					}
				} else {
					mAH = mAH1;
					mRetLeak = mRetLeak1;
					mSupLeak = mSupLeak1;
					mRetReg = mRetReg1;
					mSupReg = mSupReg1;
					supVel = supVelAH;
					retVel = retVelAH;
					AHfanHeat = fanPower_heating * 0.85;	//0.85
					AHfanPower = fanPower_heating;
					hcap = hcapacity / AFUE;

					for(int i = 0; i < numFans; i++) {			// for outside air into return cycle operation
						if(fan[i].oper == 6 && AHminutes <= 20) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 10) {				// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 14) {				// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 18) {				// vent always open during any air handler operation
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 15 && AHminutes <= 20) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
						if(fan[i].oper == 13) { 
							if((rivecFlag == 0 && AHminutes <= 20) || (rivecFlag == 1 && rivecOn == 1)){ //This allow RIVEC to operate this fan until it reaches its Exposure setpoint?
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
							ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumIN = nonRivecVentSumIN + abs(fan[i].q) * 3600 / houseVolume;
							}
						}
						if(fan[i].oper == 22 && AHminutes <= 20 && econoFlag == 0) {
							mFanCycler = fan[i].q * airDensityIN;
							mRetReg = mRetReg1 - mFanCycler;
						}
					}
				}
			}

			for(int i=0; i < numFans; i++) {
				// ***********************  20 minute minimum central fan integrated supply (CFIS) system. 20 minute minimum operation per hour. 
				if(fan[i].oper == 13) {			// fan cycler operation without exhaust fan
					if((rivecFlag == 0 && AHminutes < target && AHflag != 1 && AHflag != 2 && AHflag != 102) || (rivecFlag == 1 && rivecOn == 1 && AHflag != 1 && AHflag != 2 && AHflag != 102)){		// conditions for turning on air handler only for fan cycler operation
						AHflag = 100;
						AHminutes = AHminutes + 1;
						qAH = qAH_cool;
						supVelAH = qAH / (pow(supDiameter,2) * pi / 4);
						retVelAH = qAH / (pow(retDiameter,2) * pi / 4);
						qSupReg = -qAH * supLF + qAH;
						qRetReg = qAH * retLF - qAH;
						qRetLeak = -qAH * retLF;
						qSupLeak = qAH * supLF;
						mSupLeak = qSupLeak * airDensityIN;
						mRetLeak = qRetLeak * airDensityIN;
						mFanCycler = fan[i].q * airDensityIN;
						mSupReg = qSupReg * airDensityIN;
						mRetReg = qRetReg * airDensityIN - mFanCycler;
						mAH = qAH * airDensityIN;
						mechVentPower = mechVentPower + fanPower_cooling;			// note ah operates at cooling speed
						AHfanHeat = fanPower_cooling * .85;							// for heat
						ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumIN = nonRivecVentSumIN + abs(fan[i].q) * 3600 / houseVolume;
					}
				}
			//}

				if(fan[i].oper == 22) {						// Fan cycler operation only when economizer is off
					if(econoFlag == 0) {					// Only open CFI if economizer is off
						if(AHminutes < target && AHflag != 1 && AHflag != 2 && AHflag != 102) {		// conditions for turning on air handler only for fan cycler operation
							AHflag = 100;
							AHminutes = AHminutes + 1;
							qAH = qAH_cool;
							supVelAH = qAH / (pow(supDiameter,2) * pi / 4);
							retVelAH = qAH / (pow(retDiameter,2) * pi / 4);
							qSupReg = -qAH * supLF + qAH;
							qRetReg = qAH * retLF - qAH;
							qRetLeak = -qAH * retLF;
							qSupLeak = qAH * supLF;
							mSupLeak = qSupLeak * airDensityIN;
							mRetLeak = qRetLeak * airDensityIN;
							mFanCycler = fan[i].q * airDensityIN;
							mSupReg = qSupReg * airDensityIN;
							mRetReg = qRetReg * airDensityIN - mFanCycler;
							mAH = qAH * airDensityIN;
							mechVentPower = mechVentPower + fanPower_cooling;    // note ah operates at cooling speed
							AHfanHeat = fanPower_cooling * .85;  // for heat
							ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumIN = nonRivecVentSumIN + abs(fan[i].q) * 3600 / houseVolume;
						}
					}
				}
			}


			// [START] HRV's and ERV's===================================================================================================================================
			// (Can be RIVEC controlled so after RIVEC algorithm decision)
			for(int i=0; i < numFans; i++) {

				//Brennan. Stand-alone ERV MUST be entered two times in the input files, with half the fan power attributed to each. Same airflow, but one positive and one negative.

				if(fan[i].oper == 5) {		// Standalone HRV not synched to air handler

					if((rivecFlag == 0 && minute_hour > 30) || (rivecFlag == 1 && rivecOn == 1)) {	// on for last 30 minutes of every hour or RIVEC controlled
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;
						if(fan[i].q > 0) {
							mHRV = fan[i].q * airDensityOUT;;
							ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumIN = nonRivecVentSumIN + abs(fan[i].q) * 3600 / houseVolume;	// use if HRV not to be included in RIVEC whole house ventilation calculations
						} else {
							ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						}
					} else {
						fan[i].on = 0;
					}
				}

				if(fan[i].oper == 16) {		// HRV + Air Handler

					if((rivecFlag == 0 && minute_hour > 30) || (rivecFlag == 1 && rivecOn == 1)) {	// on for last 30 minutes of every hour or RIVEC controlled
						fan[i].on = 1;
						ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;

						mechVentPower = mechVentPower + fan[i].power;
						if(AHflag != 1 && AHflag != 2 && AHflag != 102) {			// ah is off for heat/cool so turn it on for venting
							AHflag = 100;
							AHminutes = AHminutes + 1;
							qAH = qAH_cool;
							supVelAH = qAH / (pow(supDiameter,2) * pi / 4);
							retVelAH = qAH / (pow(retDiameter,2) * pi / 4);
							qSupReg = -qAH * supLF + qAH;
							qRetReg = qAH * retLF - qAH;
							qRetLeak = -qAH * retLF;
							qSupLeak = qAH * supLF;
							mSupLeak = qSupLeak * airDensityIN;
							mAH = qAH * airDensityIN;
							mHRV_AH = abs(fan[i].q * airDensityIN) * -1.0;			// mHRV_AH needs to be a negative number - so does fan(i).q to get right mRetReg and then fan(i).q is also the exhaust from house flow
							mSupReg = qSupReg * airDensityIN;
							mRetReg = qRetReg * airDensityIN - mHRV_AH;
							mechVentPower = mechVentPower + fanPower_cooling;		// note AH operates at cooling speed
							AHfanHeat = fanPower_cooling * .85;						// for heat
						} else {													// open the outside air vent
							mHRV_AH = fan[i].q * airDensityIN;
							mRetReg = qRetReg * airDensityIN - mHRV_AH;
						}
					} else {
						fan[i].on = 0;
						mHRV_AH = 0;
					}
				}

				//From the HVI directory (as of 5/2015) of all ERV units, average SRE=0.63, LR/MT=0.51, TRE=0.52, cfm/w=1.1

				// ERV + Air Handler Copied over from ARTI code
				if(fan[i].oper == 17) {												
					
					if((rivecFlag == 0 && minute_hour > 40) || (rivecFlag == 1 && rivecOn == 1)) {	// on for last 20 minutes of every hour or RIVEC controlled
						fan[i].on = 1;
						ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						mechVentPower = mechVentPower + fan[i].power;
						if(AHflag != 1 && AHflag != 2 && AHflag != 102) {			// AH is off for heat/cool so turn it on for venting
							AHflag = 100;
							AHminutes = AHminutes + 1;
							qAH = qAH_cool;
							supVelAH = qAH / (pow(supDiameter,2) * pi / 4);
							retVelAH = qAH / (pow(retDiameter,2) * pi / 4);
							qSupReg = -qAH * supLF + qAH;
							qRetReg = qAH * retLF - qAH;
							qRetLeak = -qAH * retLF;
							qSupLeak = qAH * supLF;
							mSupLeak = qSupLeak * airDensityIN;
							mRetLeak = qRetLeak * airDensityIN;
							mAH = qAH * airDensityIN;
							mERV_AH = abs(fan[i].q * airDensityIN) * -1.0;
							mSupReg = qSupReg * airDensityIN;
							mRetReg = qRetReg * airDensityIN - mERV_AH;
							mechVentPower = mechVentPower + fanPower_cooling;		// note ah operates at cooling speed
							AHfanHeat = fanPower_cooling * .85;						// for heat
						} else {													// open the outside air vent
							mERV_AH = fan[i].q * airDensityIN;						// mass flow of ERV unit synced to Air Handler
							mRetReg = qRetReg * airDensityIN - mERV_AH;
						}
					} else {
						fan[i].on = 0;
						mERV_AH = 0;
					}
				}
			}
			// [END] HRV's and ERV's =================================================================================================================================


			// [START] Auxiliary Fan Controls ============================================================================================================

			for(int i = 0; i < numFans; i++) {

				if(fan[i].oper == 1) {							// FAN ALWAYS ON
					fan[i].on = 1;
					fan[i].q = FanQ;
					//fan[i].on = 0;	// Use this to disable the whole-house exhaust fan
					mechVentPower = mechVentPower + fan[i].power;				// vent fan power
					if(fan[i].q > 0) {							// supply fan - its heat needs to be added to the internal gains of the house
						fanHeat = fan[i].power * .84;			// 16% efficiency for the particular fan used in this study.
						ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
					}
					else											// exhasut fan
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;				
						
				} else if(fan[i].oper == 2) {					// FIXED SCHEDULE BATHROOM
					if(HOUR == 7 && minute_hour > 30 && minute_hour < 61) {
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 3) {					// FIXED SCHEDULE KITCHEN
					if(HOUR > 17 && HOUR <= 18) {
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;			// vent fan power
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 4) {					// FIXED SCHEDULE EXHAUST FAN
					if(hcFlag == 1) {  // if heating
						if(HOUR > 1 && HOUR <= 5)
							fan[i].on = 0;
						else {
							fan[i].on = 1;
							mechVentPower = mechVentPower + fan[i].power;		// vent fan power
							ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						}
					}
					if(hcFlag == 2) {  // if cooling
						if(HOUR > 15 && HOUR <= 19) {
							fan[i].on = 0;
						} else {
							fan[i].on = 1;
							mechVentPower = mechVentPower + fan[i].power;		// vent fan power
							ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						}
					}

				} else if(fan[i].oper == 19) {					// DRYER FAN
					if(weekendFlag == 1) {						// Three hours of operation, two days per week
						if(HOUR > 12 && HOUR <= 15) {
							fan[i].on = 1;
							mechVentPower = mechVentPower + fan[i].power;
							ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
							//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						} else
							fan[i].on = 0;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 21) {					// Economizer
					if(econoFlag == 1) {
						fan[i].on = 1;
						AHfanPower = AHfanPower + fan[i].power;			// vent fan power
						ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumIN = nonRivecVentSumIN + abs(fan[i].q) * 3600 / houseVolume;
						economizerRan = 1;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 23) {					// DYNAMIC SCHEDULE DRYER
					if(dryerFan == 1) {
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 24) {					// DYNAMIC SCHEDULE KITCHEN FAN
					if(kitchenFan == 1) {
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 25) {					// DYNAMIC SCHEDULE BATHROOM 1
					if(bathOneFan == 1) {
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 26) {					// DYNAMIC SCHEDULE BATHROOM 2
					if(bathTwoFan == 1) {
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					} else
						fan[i].on = 0;

				} else if(fan[i].oper == 27) {					// DYNAMIC SCHEDULE BATHROOM 3. Brennan, we can redirect this fan.oper=27 to look at the bathOneFan value to increaes usage for 6 occupants.
					//if(bathThreeFan == 1) {
					if(bathOneFan == 1) {
						fan[i].on = 1;
						mechVentPower = mechVentPower + fan[i].power;
						ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
						//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
					} else
						fan[i].on = 0;

				//} else if(fan[i].oper == 28) {					// Single Cut-Off Ventilation Temperature Controller. Brennan.
				//	if(weatherTemp >= ventcutoff) {
				//		fan[i].on = 1;
				//		mechVentPower = mechVentPower + fan[i].power;
				//		ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//		//nonRivecVentSumOUT = nonRivecVentSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//	} else
				//		fan[i].on = 0;

				//} else if(fan[i].oper == 29) {					// Two Cut-Off Ventilation Temperature Controller. Brennan.
				//	if(weatherTemp >= ventcutoff && weatherTemp < ventcutoff2) {
				//		fan[i].on = 1; //Brennan, this needs to equal 50% fan airflow.
				//		fan[i].q = FanQ * 0.50 ; //FanQ is set outside the loop, so it is fixed, based on the imnput file. This modifies it by cutting in half.
				//		fan[i].power = FanP * 0.50 ;
				//		mechVentPower = mechVentPower + fan[i].power; 
				//		ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//	} else
				//		if(weatherTemp >= ventcutoff2) {
				//			fan[i].on = 1; //Brennan, this needs to equal 100% fan airflow
				//			fan[i].q = FanQ;
				//			fan[i].power = FanP;
				//			mechVentPower = mechVentPower + fan[i].power;
				//			ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//		} else
				//			fan[i].on = 0;

				//} else if(fan[i].oper == 30) {// Continuous, Infiltration-Balanced Ventilation Temperature Controller ("Gold Standard"). Brennan.
				//	if(numStories == 1){
				//		AIM2 = pow(abs((tempHouse - 273.15) - weatherTemp) , 0.67) * 0.069 * C; //estimated stack infiltration (m3/s). Alwasy positive.
				//		AEQaim2FlowDiff = (Aeq * houseVolume / 3600) - AIM2; //difference between 62.2-2013 AEQ flow rate (m3/s) and the estimated infiltration. Sometimes negative.
				//		qFanFlowRatio = AEQaim2FlowDiff / (Aeq * houseVolume / 3600); //dimensionless ratio of flows. Sometimes negative.
				//			if(AIM2 < (Aeq * houseVolume / 3600)){ //tests if infiltration exceeds Aeq
				//				fan[i].on = 1;
				//				fan[i].q = FanQ * qFanFlowRatio; //adjusts fan flow based on ratio of AIM2 / Aeq
				//				fan[i].power = FanP * qFanFlowRatio; //adjusts fan power based on ratio of AIM2 / Aeq
				//				mechVentPower = mechVentPower + fan[i].power ;
				//				ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume ;  
				//			} else
				//				fan[i].on = 0;
				//	} else 
				//		if(numStories == 2){
				//			AIM2 = pow(abs((tempHouse - 273.15) - weatherTemp) , 0.67) * 0.089 * C;
				//			AEQaim2FlowDiff = (Aeq * houseVolume / 3600) - AIM2;
				//			qFanFlowRatio = AEQaim2FlowDiff / (Aeq * houseVolume / 3600);
				//				if(AIM2 < (Aeq * houseVolume / 3600)){
				//					fan[i].on = 1;
				//					fan[i].q = FanQ * qFanFlowRatio;
				//					fan[i].power = FanP * qFanFlowRatio;
				//					mechVentPower = mechVentPower + fan[i].power ;
				//					ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume ; 
				//				} else 
				//					fan[i].on = 0;
				//	} 


				//} else if(fan[i].oper == 31) {// Continuous, Infiltration-Balanced Ventilation Temperature Controller ("Gold Standard"). Brennan. Quadrature!
				//	if(numStories == 1){
				//		AIM2 = 0.069 * C * pow(abs((tempHouse - 273.15) - weatherTemp), n); //estimated stack infiltration (m3/s).
				//		AEQaim2FlowDiff = pow((Aeq * houseVolume / 3600),2) - pow(AIM2,2); //difference between squares of 62.2-2013 AEQ flow rate (m3/s) and the estimated infiltration. Tells us if AIM2 inf > Aeq
				//		if(AEQaim2FlowDiff > 0){ 
				//			fan[i].on = 1;
				//			qFanFlowRatio = sqrt(AEQaim2FlowDiff) / (Aeq * houseVolume / 3600); //sqrt of the squared differences solves for the required fan flow, this is the ratio of the req fan flow to the Aeq
				//			fan[i].q = FanQ * qFanFlowRatio;
				//			fan[i].power = FanP * qFanFlowRatio;
				//			mechVentPower = mechVentPower + fan[i].power ;
				//			ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//		} else
				//			fan[i].on = 0;
				//	} else 
				//			if(numStories == 2){
				//			AIM2 = 0.089 * C * pow(abs((tempHouse - 273.15) - weatherTemp), n); 
				//			AEQaim2FlowDiff = pow((Aeq * houseVolume / 3600),2) - pow(AIM2,2); 
				//				if(AEQaim2FlowDiff > 0){
				//					fan[i].on = 1;
				//					qFanFlowRatio = sqrt(AEQaim2FlowDiff) / (Aeq * houseVolume / 3600); //
				//					fan[i].q = FanQ * qFanFlowRatio;
				//					fan[i].power = FanP * qFanFlowRatio;
				//					mechVentPower = mechVentPower + fan[i].power ;
				//					ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//				} else
				//					fan[i].on = 0;
								


					} 
				}
			//}
			// [END] Auxiliary Fan Controls ========================================================================================================

			
				// [END] ---------------------FAN 50---------- RIVEC OPERATION BASED ON CONTROL ALGORITHM v6

				// ---------------------FAN 30---------------------- OLD RIVEC OPERATION BASED ON CONTROL ALGORITHM v1
				////if(fan[i].oper == 30) {
				////	if(minute_hour == 1 || minute_hour == 11 || minute_hour == 21 || minute_hour == 31 || minute_hour == 41 || minute_hour == 51) {
				////		rivecOn = 1;		// start with controlled fan on as default					
				////		if(hcFlag == 1) {									// HEATING TIMES
				////			if(HOUR >= 10 && HOUR < 22) {					// base time period
				////				if(relDose <= 1 && relExp <= 0.8)
				////					rivecOn = 0;
				////				if(nonRivecVentSum < Aeq)
				////					rivecOn = 1;
				////			} else if(HOUR >= 22 || HOUR < 2) {				// pre-peak time period
				////				if(relDose <= 1 && relExp <= 0.8)
				////					rivecOn = 0;
				////				if(nonRivecVentSum > 1.25 * Aeq)
				////					rivecOn = 0;
				////			} else if(HOUR >= 2 && HOUR < 6) {				// peak time period
				////				rivecOn = 0;								// always off
				////			} else {                                        // post-peak time period
				////				if(relDose <= 1 && relExp <= 0.8)
				////					rivecOn = 0;
				////				if(nonRivecVentSum > 1.25 * Aeq)
				////					rivecOn = 0;
				////			}
				////		} else {											// COOLING TIMES
				////			if(HOUR >= 22 || HOUR < 10) {					// base time
				////				if(relDose <= 1 && relExp <= .8)
				////					rivecOn = 0;
				////				if(nonRivecVentSum < Aeq)
				////					rivecOn = 1;
				////			} else if(HOUR >= 10 && HOUR < 14) { 			// prepeak
				////				if(relDose <= 1 && relExp <= 0.8)
				////					rivecOn = 0;
				////				if(nonRivecVentSum > 1.25 * Aeq)
				////					rivecOn = 0;
				////			} else if(HOUR >= 14 && HOUR < 18) {			// peak
				////				rivecOn = 0;								// always off
				////			} else {										// post peak
				////				if(relDose <= 1 && relExp <= 0.8)
				////					rivecOn = 0;
				////				if(nonRivecVentSum > 1.25 * Aeq)
				////					rivecOn = 0;
				////			}

				////		}
				////	}

				//	//RIVEC decision after all other fans added up
				//	if(rivecOn == 0)										//RIVEC has turned off this fan
				//		fan[i].on = 0;
				//	else {													
				//		fan[i].on = 1;
				//		rivecMinutes++;
				//		mechVentPower = mechVentPower + fan[i].power;		// vent fan power
				//		if(fan[i].q > 0) {									// supply fan - its heat needs to be added to the internal gains of the house
				//			fanHeat = fan[i].power * .84;					// 16% efficiency for the particular fan used in this study.
				//			ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
				//		} else												// ex fan
				//			ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//	}
				//}
				//---------------------FAN 30 end----------------------

				//// ---------------------FAN 31---------------------- OLD RIVEC OPERATION BASED ON CONTROL ALGORITHM v2
				//if(fan[i].oper == 31) {
				//	if(minute_hour == 1 || minute_hour == 11 || minute_hour == 21 || minute_hour == 31 || minute_hour == 41 || minute_hour == 51) {
				//		rivecOn = 1;		// start with controlled fan on as default					
				//		if(hcFlag == 1) {									// HEATING TIMES
				//			if(HOUR >= 10 && HOUR < 22) {					// base time period
				//				if(relDose <= 1 && relExp <= .8)
				//					rivecOn = 0;
				//				//if(nonRivecVentSum < Aeq)
				//				//rivecOn = 1; (removed from revised algorithm)
				//			} else if(HOUR >= 22 || HOUR < 2) {				// pre-peak time period
				//				if(relDose <= 1 && relExp <= 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			} else if(HOUR >= 2 && HOUR < 6) {				// peak time period
				//				rivecOn = 0;								// always off
				//			} else {                                        // post-peak time period
				//				if(relDose <= 1 && relExp <= 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			}
				//		} else {											// COOLING TIMES
				//			if(HOUR >= 22 || HOUR < 10) {					// base time
				//				if(relDose <= 1 && relExp <= .8)
				//					rivecOn = 0;
				//				//IF nonRivecVentSum < Aeq THEN rivecOn = 1 (removed from revised algorithm)
				//			} else if(HOUR >= 10 && HOUR < 14) { 			// prepeak
				//				if(relDose <= 1 && relExp <= 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			} else if(HOUR >= 14 && HOUR < 18) {			// peak
				//				rivecOn = 0;								// always off
				//			} else {										// post peak
				//				if(relDose <= 1 && relExp <= 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			}

				//		}
				//	}

				//	//RIVEC decision after all other fans added up
				//	if(rivecOn == 0)										//RIVEC has turned off this fan
				//		fan[i].on = 0;
				//	else {													
				//		fan[i].on = 1;
				//		rivecMinutes++;
				//		mechVentPower = mechVentPower + fan[i].power;		// vent fan power
				//		if(fan[i].q > 0) {									// supply fan - its heat needs to be added to the internal gains of the house
				//			fanHeat = fan[i].power * .84;					// 16% efficiency for the particular fan used in this study.
				//			ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
				//		} else												// ex fan
				//			ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//	}
				//}
				//---------------------FAN 31 end----------------------

				//---------------------FAN 50---------- RIVEC OPERATION BASED ON CONTROL ALGORITHM v5
				//if(fan[i].oper == 50) {
				//	if(minute_hour == 1 || minute_hour == 11 || minute_hour == 21 || minute_hour == 31 || minute_hour == 41 || minute_hour == 51) {
				//	// rivecOn = 1 or 0: 1 = whole-house fan ON, 0 = whole-house fan OFF
				//	rivecOn = 0;
				//		if(HOUR >= baseStart || HOUR < baseEnd ) {        		// BASE Time Period
				//			
				//			if(occupied[HOUR]) {				            	// Base occupied
				//				if(relExp > 0.8 && nonRivecVentSum < Aeq) 
				//					rivecOn = 1;

				//			} else {						                	// Base unoccupied
				//				if(relExp > expLimit && nonRivecVentSum < 1.25 * Aeq)
				//					rivecOn = 1;
				//			}

				//		} else if(HOUR >= peakStart && HOUR < peakEnd) {		// PEAK Time Period
				//			rivecOn = 0;										// Always off

				//		} else if(HOUR >= recoveryStart && HOUR < recoveryEnd) {// RECOVERY Time Period
				//	
				//			if (occupied[HOUR]) {								// Recovery occupied
				//				if(relExp > 1 && nonRivecVentSum < 1.25 * Aeq)
				//					rivecOn = 1;

				//			} else {											// Recovery unoccupied
				//				if(relExp > expLimit && nonRivecVentSum < 1.25 * Aeq)
				//					rivecOn = 1;
				//			}
				//		}
				//	}

				//	// RIVEC fan operation after algorithm decision
				//	if(rivecOn) {	  												// RIVEC has turned ON this fan
				//		fan[i].on = 1;
				//		mechVentPower = mechVentPower + fan[i].power;				// vent fan power
				//		if(fan[i].q > 0) { 											// supply fan - its heat needs to be added to the internal gains of the house
				//			fanHeat = fan[i].power * .84;							// 16% efficiency for this fan
				//			ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
				//		} else { 													// exhaust fan
				//			ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//		}
				//		rivecMinutes++;
				//	}
				//	else {  														// limited set of fans controlled by RIVEC. Only supply, exhaust and HRV are controlled
				//		fan[i].on = 0;

				//	}
				//}
				// --------------------FAN 50 end----------------------


				////---------------------FAN 50---------- RIVEC OPERATION BASED ON CONTROL ALGORITHM v4 WJNT May 2011
				//if(fan[i].oper == 50) {
				//	// if(minute_hour == 1 || minute_hour == 11 || minute_hour == 21 || minute_hour == 31 || minute_hour == 41 || minute_hour == 51) {
				//	if(minute_hour == 1 || minute_hour == 16 || minute_hour == 31 || minute_hour == 46) {

				//		// rivecOn = 1 or 0: 1 = whole-house fan ON, 0 = whole-house fan OFF
				//		if(hcFlag == 1) {      										// HEATING TIMES
				//			if((HOUR >= 0 && HOUR < 4) || (HOUR >= 12)) {        	// BASE Time Period
				//				if(occupied[HOUR] == 1) {                        	// Occupied
				//					rivecOn = 1;									// Turn rivec fan ON
				//					if(relDose < 1 && relExp < .8) {
				//						if(nonRivecVentSum > Aeq) {
				//							rivecOn = 0;
				//						}											
				//					}									
				//				} else if(occupied[HOUR] == 0) {                	// Unoccupied
				//					//if(relDose > 1.2 || relExp > expLimit)
				//					if(relDose > 1.2 || relExp > 2)
				//						rivecOn = 1;
				//					//if(relDose < 1 && relExp < expLimit)
				//					if(relDose < 1 && relExp < 2)
				//						rivecOn = 0;
				//					if(nonRivecVentSum > Aeq)
				//						rivecOn = 0;									
				//				}
				//			} else if(HOUR >= 4 && HOUR < 8) {						// PEAK Time Period
				//				rivecOn = 0;										// Always off
				//			} else if(HOUR >= 8 && HOUR < 12) {         			// RECOVERY Time Period
				//				if(occupied[HOUR] == 0)
				//					rivecOn = 0;
				//				if(nonRivecVentSum < 1.25 * Aeq) {
				//					if(relExp > expLimit)
				//					//if(relExp > 1)
				//						rivecOn = 1;
				//				}
				//				if(relExp < 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			}
				//		} else {    												// COOLING TIMES
				//			if(HOUR >= 22 || (HOUR >= 0 && HOUR < 14)) {           	// BASE Time Period
				//				if(occupied[HOUR] == 1) {                           // Occupied
				//					rivecOn = 1;									// Turn rivec fan ON
				//					if(relDose < 1 && relExp < .8) {
				//						if(nonRivecVentSum > Aeq)
				//							rivecOn = 0;
				//					}
				//				} else if(occupied[HOUR] == 0) {	             	// Unoccupied
				//					if(relDose > 1.2 || relExp > expLimit)
				//					//if(relDose > 1.2 || relExp > 2)
				//						rivecOn = 1;
				//					if(relDose < 1 && relExp < expLimit)
				//					//if(relDose < 1 && relExp < 2)
				//						rivecOn = 0;
				//					if(nonRivecVentSum > Aeq)
				//						rivecOn = 0;
				//				}
				//			} else if(HOUR >= 14 && HOUR < 18) {                 	// PEAK Time Period
				//				rivecOn = 0;                                		// Always off
				//			} else if(HOUR >= 18 && HOUR < 22) {         			// RECOVERY Time Period
				//				if(occupied[HOUR] == 0)
				//					rivecOn = 0;
				//				if(nonRivecVentSum < 1.25 * Aeq) {
				//					if(relDose > 1 || relExp > expLimit)
				//					//if(relDose > 1 || relExp > 1)
				//						rivecOn = 1;
				//				}
				//				if(relExp < 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			}
				//		}
				//	}
				//	// RIVEC decision after all other fans added up
				//	if(rivecOn == 0)	  											// RIVEC has turned off this fan
				//		fan[i].on = 0;
				//	else {  														// limited set of fans controlled by RIVEC. Only supply, exhaust and HRV are controlled
				//		rivecMinutes++;
				//		fan[i].on = 1;
				//		mechVentPower = mechVentPower + fan[i].power;				// vent fan power
				//		if(fan[i].q > 0) { 											// supply fan - its heat needs to be added to the internal gains of the house
				//			fanHeat = fan[i].power * .84;							// 16% efficiency for the particular fan used in this study.
				//			ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
				//		} else { 													// exhaust fan
				//			ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//		}
				//	}
				//}
				//// --------------------FAN 50 end----------------------

				//---------------------FAN 51---------- RIVEC OPERATION BASED ON CONTROL ALGORITHM v3 WJNT May 2011
				//if(fan[i].oper == 51) {
				//	if(minute_hour == 1 || minute_hour == 11 || minute_hour == 21 || minute_hour == 31 || minute_hour == 41|| minute_hour == 51) {
				//		if(hcFlag == 1) {      										// HEATING TIMES
				//			if((HOUR >= 0 && HOUR < 4) || (HOUR >= 12)) {        	// BASE Time Period
				//				if(occupied[HOUR] == 1) {                        	// Occupied
				//					rivecOn = 1;									// Turn rivec fan ON
				//					if(relDose < 1 && relExp < .8) {
				//						if(nonRivecVentSum > Aeq) {
				//							rivecOn = 0;
				//						}											
				//					}									
				//				} else if(occupied[HOUR] == 0) {                	// Unoccupied
				//					if(relDose > 1.2 || relExp > 2)
				//						rivecOn = 1;
				//					if(relDose < 1 && relExp < 2)
				//						rivecOn = 0;
				//					if(nonRivecVentSum > Aeq)
				//						rivecOn = 0;									
				//				}
				//			} else if(HOUR >= 4 && HOUR < 8) {						// PEAK Time Period
				//				rivecOn = 0;										// Always off
				//			} else if(HOUR >= 8 && HOUR < 12) {         			// RECOVERY Time Period
				//				if(nonRivecVentSum < 1.25 * Aeq) {
				//					if(relDose > 1 || relExp > 1)
				//						rivecOn = 1;
				//				}
				//				if(relDose < 1 && relExp < 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			}
				//		} else {    												// COOLING TIMES
				//			if(HOUR >= 22 || (HOUR >= 0 && HOUR < 14)) {           	// BASE Time Period
				//				if(occupied[HOUR] == 1) {                           // Occupied
				//					rivecOn = 1;									// Turn rivec fan ON
				//					if(relDose < 1 && relExp < .8) {
				//						if(nonRivecVentSum > Aeq)
				//							rivecOn = 0;
				//					}
				//				} else if(occupied[HOUR] == 0) {					// Unoccupied
				//					if(relDose > 1.2 || relExp > 2)
				//						rivecOn = 1;
				//					if(relDose < 1 && relExp < 2)
				//						rivecOn = 0;
				//					if(nonRivecVentSum > Aeq)
				//						rivecOn = 0;
				//				}
				//			} else if(HOUR >= 14 && HOUR < 18) {                 	// PEAK Time Period
				//				rivecOn = 0;                                		// Always off
				//			} else if(HOUR >= 18 && HOUR < 22) {         			// RECOVERY Time Period
				//				if(nonRivecVentSum < 1.25 * Aeq) {
				//					if(relDose > 1 || relExp > 1)
				//						rivecOn = 1;
				//				}
				//				if(relDose < 1 && relExp < 1)
				//					rivecOn = 0;
				//				if(nonRivecVentSum > 1.25 * Aeq)
				//					rivecOn = 0;
				//			}
				//		}
				//	}
				//	// RIVEC decision after all other fans added up
				//	if(rivecOn == 0)	  											// RIVEC has turned off this fan
				//		fan[i].on = 0;
				//	else {  														// limited set of fans controlled by RIVEC. Only supply, exhaust and HRV are controlled
				//		rivecMinutes++;
				//		fan[i].on = 1;
				//		mechVentPower = mechVentPower + fan[i].power;								// vent fan power
				//		if(fan[i].q > 0) { 											// supply fan - its heat needs to be added to the internal gains of the house
				//			fanHeat = fan[i].power * .84;							// 16% efficiency for the particular fan used in this study.
				//			ventSumIN = ventSumIN + abs(fan[i].q) * 3600 / houseVolume;
				//		} else { 													// exhaust fan
				//			ventSumOUT = ventSumOUT + abs(fan[i].q) * 3600 / houseVolume;
				//		}
				//	}
				//}
				// ---------------------FAN 51 end----------------------


			//} // [End RIVEC Fans] =======================================================================================================================================



			// [START] Hybrid Systems ================================================================================================================================
			// Block off passive stacks/flues when the RIVEC fan is running for hybrid ventilation strategy
			
			//if(rivecOn)
			//	numFlues = 0;
			//else
			//	numFlues = numFluesActual;

			// [END] Hybrid Systems ==================================================================================================================================

			// [START] Equipment Model ===============================================================================================================================
			evapcap = 0;
			latcap = 0;
			capacity = 0;
			capacityh = 0;
			powercon = 0;
			compressorPower = 0;

			if(AHflag == 0) {										// AH OFF
				capacityc = 0;
				capacityh = 0;
				powercon = 0;
				compressorPower = 0;
			} else if(AHflag == 100) {								// Fan on no heat/cool
				capacityh = AHfanHeat;								// Include fan heat
				capacityc = 0;
			} else if(AHflag == 102) {								// End if heat cycle
				capacityh = .25 * hcapacity + AHfanHeat;			// NOTE 0.25 burner capacity at 1/4 during cool down
				capacityc = 0;
			} else if(AHflag == 1) {								// Heating
				capacityh = hcapacity + AHfanHeat;					// include fan heat in furnace capacity (converted to W in main program)
				capacityc = 0;
			} else {												// we have cooling
				Toutf = (tempOut - 273.15) * (9.0 / 5.0) + 32;
				tretf = (tempOld[11] - 273.15 + AHfanHeat / 1005 / mAH) * (9.0 / 5.0) + 32;		// return in F used for capacity includes fan heat
				dhret = AHfanHeat / mAH / 2326;										// added to hret in capacity caluations for wet coil - converted to Btu/lb

				// the following corerctions are for TXV only
				// test for wet/dry coil using SHR calculation
				SHR = 1 - 50 * (HR[1] - .005);										// using humidity ratio - note only because we have small indoor dry bulb range

				if(SHR < .25)
					SHR = .25;  //setting low limit
				if(SHR > 1)
					SHR = 1;
				if(SHR == 1) { // dry coil
					// charge correction for capacity (dry coil)
					if(charge < .725) {
						chargecapd = 1.2 + (charge - 1);
					} else if(charge >= .725) {
						chargecapd = .925;
					}
					// charge correction for EER (dry coil)
					if(charge < 1) {
						chargeeerd = 1.04 + (charge - 1) * .65;
					} else if(charge >= 1) {
						chargeeerd = 1.04 - (charge - 1) * .35;
					}
					capacity = (capacityari * 1000) * .91 * qAHcorr * chargecapd * ((-.00007) * pow((Toutf - 82),2) - .0067 * (Toutf - 82) + 1);
					// EER for dry coil
					EER = EERari * qAHcorr * chargeeerd * ((-.00007) * pow((Toutf - 82),2) - .0085 * (Toutf - 82) + 1);
				} else {															// wet coil
					// charge correction for capacity (wet coil)
					if(charge < .85) {
						chargecapw = 1 + (charge - .85);
					} else if(charge >= .85) {
						chargecapw = 1;
					}
					// charge correction for EER (wet coil)
					if(charge < .85) {
						chargeeerw = 1 + (charge - .85) * .9;
					} else if(charge >= .85 && charge <= 1) {
						chargeeerw = 1;
					} else if(charge > 1) {
						chargeeerw = 1 - (charge - 1) * .35;
					}
					capacity = capacityari * 1000 * (1 + ((hret + dhret) - 30) * .025) * qAHcorr * chargecapw * ((-.00007) * pow((Toutf - 95),2) - .0067 * (Toutf - 95) + 1);
					// EER for wet coil
					EER = EERari * qAHcorr * chargeeerw * ((-.00007) * pow((Toutf - 95),2) - .0085 * (Toutf - 95) + 1);
				}

				// split sensible and latent capacities and convert capacity Btu/h to W
				// calculation of power used and total cacapity for air conditioning
				compressorPower = capacity / EER; //compressor power

				// SHR ramps up for first three minutes of operation
				// compTime tracks the number of minutes the compressor is on for
				if(compTime == 1) {
					SHR = SHR + 2 / 3 * (1 - SHR);
				} else if(compTime == 2) {
					SHR = SHR + 1 / 3 * (1 - SHR);
				}

				capacityc = SHR * capacity / 3.413;									// sensible (equals total if SHR=1)
				// correct the sensible cooling for fan power heating
				capacityc = capacityc - AHfanHeat;
				// tracking mass on coil (Mcoil) including condensation and evaporation - do this in main program
				evapcap = 0;
				latcap = 0;
			}

			// Tracking Coil Moisture
			if(AHflag == 2) {
				if(SHR < 1) {
					latcap = (1 - SHR) * capacity / 3.413;							// latent capacity J/s
					Mcoil = Mcoilprevious + latcap / 2501000 * dtau;				// condenstion in timestep kg/s * time
				} else {
					if(Mcoil > 0) {													// if moisture on coil but no latcap, then we evaporate coil moisture until Mcoil = zero
						latcap = -.3 * capacityraw / 1800 * 2501000;				// evaporation capacity J/s - this is negative latent capacity
						evapcap = latcap;
						Mcoil = Mcoilprevious - .3 * capacityraw / 1800 * dtau;		// evaporation in timestep kg/s * time
					} else {														// no latcap and dry coil
						latcap = 0;
					}
				}
			} else if(AHflag == 100) {												// then we have no cooling, but the fan is on
				if(Mcoil > 0) {														// if moisture on coil but no latcap, then we evaporate coil moisture until Mcoil = zero
					latcap = -.3 * capacityraw / 1800 * 2501000;					// evaporation capacity J/s - this is negative latent capacity
					evapcap = latcap;
					Mcoil = Mcoilprevious - .3 * capacityraw / 1800 * dtau;			// evaporation in timestep kg/s * time
				} else {															// no latcap and dry coil
					latcap = 0;
				}
			}

			if(Mcoil < 0)
				Mcoil = 0;
			if(Mcoil > .3 * capacityraw)
				Mcoil = .3 * capacityraw;											// maximum mass on coil is 0.3 kg per ton of cooling
			Mcoilprevious = Mcoil;													// maybe put this at top of the hour
			// [END] Equipment Model ======================================================================================================================================

			// [START] Moisture Balance ===================================================================================================================================
			for(int i=0; i < 5; i++) {
				hrold[i] = HR[i];
			}

			// Call moisture subroutine
			sub_moisture(HR, hrold, M1, M12, M15, M16, Mw5, dtau, matticenvout, mCeiling, mSupAHoff, mRetAHoff,
				matticenvin, HROUT, mSupLeak, mAH, mRetReg, mRetLeak, mSupReg, latcap, mHouseIN, mHouseOUT,
				latentLoad, mFanCycler, mHRV_AH, mERV_AH, ERV_TRE, MWha, airDensityIN, airDensityOUT);

			//Coefficients for saturation vapor pressure over ice -100 to 0C. ASHRAE HoF.
			C1 = -5.6745359E+03;
			C2 = 6.3925247E+00;
			C3 = -9.6778430E-03;
			C4 = 6.2215701E-07;
			C5 = 2.0747825E-09;
			C6 = -9.4840240E-13;
			C7 = 4.1635019E+00;

			//Coefficients for saturation vapor pressure over liquid water 0 to 200C. ASHRAE HoF.
			C8 = -5.8002206E+03;
			C9 = 1.3914993E+00;
			C10 = -4.8640239E-02;
			C11 = 4.1764768E-05;
			C12 = -1.4452093E-08;
			C13 = 6.5459673E+00;

			//Calculate Saturation Vapor Pressure, Equations 5 and 6 in ASHRAE HoF
			if((tempHouse-273.15) <= 0){
				SatVaporPressure = exp((C1/tempHouse)+(C2)+(C3*tempHouse)+(C4*pow(tempHouse, 2))+(C5*pow(tempHouse, 3))+(C6*pow(tempHouse, 4))+(C7*log(tempHouse)));
			} else{
				SatVaporPressure = exp((C8/tempHouse)+(C9)+(C10*tempHouse)+(C11*pow(tempHouse, 2))+(C12*pow(tempHouse, 3))+(C13*log(tempHouse)));
			}

			//SatVaporPressure = (exp(77.345+(0.0057*tempHouse)-(7235/tempHouse))/(pow(tempHouse,8.2))); //Brennan's old method of calculating the Saturation vapor pressure.

			//Calculate Saturation Humidity Ratio, Equation 23 in ASHRAE HoF
			HRsaturation = 0.621945*(SatVaporPressure/(pRef-SatVaporPressure)); 

			if(HR[1] == 0)
				HR[1] = HROUT;
			if(HR[3] > HRsaturation) // Previously set by Iain to 0.02. Here we've replaced it with the saturation humidity ratio (Ws).
				HR[3] = HRsaturation; //consider adding calculate saturation humidity ratio by indoor T and Pressure and lmit HR[3] to that.

			// hret is in btu/lb and is used in capacity calculations
			hret = .24 * ((b[11] - 273.15) * 9 / 5 + 32) + HR[1] * (1061 + .444 * ((b[11] - 273.15) * 9 / 5 + 32));



			// [END] Moisture Balance =======================================================================================================================================

			// [START] Heat and Mass Transport ==============================================================================================================================
			mCeilingOld = -1000;														// inital guess
			mainIterations = 0;
			limit = C / 10;
			if(limit < .00001)
				limit = .00001;

			// Ventilation and heat transfer calculations
			while(1) {
				mainIterations = mainIterations + 1;	// counting # of temperature/ventilation iterations
				flag = 0;

				while(1) {
					// Call houseleak subroutine to calculate air flow. Brennan added the variable mCeilingIN to be passed to the subroutine. Re-add between mHouseIN and mHouseOUT
					sub_houseLeak(AHflag, flag, windSpeed, direction, tempHouse, tempAttic, tempOut, C, n, h, R, X, numFlues,
						flue, wallFraction, floorFraction, Sw, flueShelterFactor, numWinDoor, winDoor, numFans, fan, numPipes,
						Pipe, mIN, mOUT, Pint, mFlue, mCeiling, mFloor, atticC, dPflue, dPceil, dPfloor, Crawl,
						Hfloor, rowOrIsolated, soffitFraction, Patticint, wallCp, airDensityRef, airTempRef, mSupReg, mAH, mRetLeak, mSupLeak,
						mRetReg, mHouseIN, mHouseOUT, supC, supn, retC, retn, mSupAHoff, mRetAHoff, Aeq, airDensityIN, airDensityOUT, airDensityATTIC, ceilingC, houseVolume, windPressureExp, Q622);
					//Yihuan : put the mCeilingIN on comment 
					flag = flag + 1;

					if(abs(mCeilingOld - mCeiling) < limit || flag > 5)
						break;
					else
						mCeilingOld = mCeiling;

					// call atticleak subroutine to calculate air flow to/from the attic
					sub_atticLeak(flag, windSpeed, direction, tempHouse, tempOut, tempAttic, atticC, atticPressureExp, h, roofPeakHeight,
						flueShelterFactor, Sw, numAtticVents, atticVent, soffit, mAtticIN, mAtticOUT, Patticint, mCeiling, rowOrIsolated,
						soffitFraction, roofPitch, roofPeakOrient, numAtticFans, atticFan, airDensityRef, airTempRef, mSupReg, mRetLeak, mSupLeak, matticenvin,
						matticenvout, dtau, mSupAHoff, mRetAHoff, airDensityIN, airDensityOUT, airDensityATTIC);
				}

				// adding fan heat for supply fans, internalGains1 is from input file, fanHeat reset to zero each minute, internalGains is common
				internalGains = internalGains1 + fanHeat;

				bsize = sizeof(b)/sizeof(b[0]);

				// Call heat subroutine to calculate heat exchange
				sub_heat(tempOut, airDensityRef, airTempRef, mCeiling, AL4, windSpeed, ssolrad, nsolrad, tempOld, atticVolume, houseVolume, sc, b, ERRCODE, TSKY,
					floorArea, roofPitch, ductLocation, mSupReg, mRetReg, mRetLeak, mSupLeak, mAH, supRval, retRval, supDiameter,
					retDiameter, supArea, retArea, supThickness, retThickness, supVolume, retVolume, supCp, retCp, supVel, retVel, suprho,
					retrho, pRef, HROUT, diffuse, UA, matticenvin, matticenvout, mHouseIN, mHouseOUT, planArea, mSupAHoff,
					mRetAHoff, solgain, windowS, windowN, windowWE, winShadingCoef, mFanCycler, roofPeakHeight, h, retLength, supLength,
					roofType, M1, M12, M15, M16, roofRval, rceil, AHflag, dtau, mERV_AH, ERV_SRE, mHRV, HRV_ASE, mHRV_AH, SBETA, CBETA, L, dec, Csol, idirect,
					capacityc, capacityh, evapcap, internalGains, bsize, airDensityIN, airDensityOUT, airDensityATTIC, airDensitySUP, airDensityRET, numStories, storyHeight);

				if(abs(b[0] - tempAttic) < .2) {	// Testing for convergence

					tempAttic        = b[0];					
					tempInnerSheathN = b[1];
					tempOuterSheathN = b[2];
					tempInnerSheathS = b[3];
					tempOuterSheathS = b[4];					
					tempWood         = b[5];
					tempCeiling      = b[6];
					tempAtticFloor   = b[7];
					tempInnerGable   = b[8];
					tempOuterGable   = b[9];
					tempRetSurface   = b[10];
					tempReturn       = b[11];
					tempHouseMass    = b[12];
					tempSupSurface   = b[13];					
					tempSupply       = b[14];
					tempHouse        = b[15];

					break;
				}

				if(mainIterations > 10) {			// Assume convergence

					tempAttic        = b[0];
					tempInnerSheathN = b[1];
					tempOuterSheathN = b[2];
					tempInnerSheathS = b[3];
					tempOuterSheathS = b[4];					
					tempWood         = b[5];
					tempCeiling      = b[6];
					tempAtticFloor   = b[7];
					tempInnerGable   = b[8];
					tempOuterGable   = b[9];
					tempRetSurface   = b[10];
					tempReturn       = b[11];
					tempHouseMass	 = b[12];
					tempSupSurface   = b[13];					
					tempSupply       = b[14];
					tempHouse	     = b[15];

					break;
				}
				tempAttic = b[0];
				tempHouse = b[15];
			}

			// setting "old" temps for next timestep to be current temps:
			tempOld[0]  = tempAttic;			// Node 1 is the Attic Air
			tempOld[1]  = tempInnerSheathN;		// Node 2 is the Inner North Sheathing
			tempOld[2]  = tempOuterSheathN;		// Node 3 is the Outer North Sheathing
			tempOld[3]  = tempInnerSheathS;		// Node 4 is the Inner South Sheathing
			tempOld[4]  = tempOuterSheathS;		// Node 5 is the Outer South Sheathing
			tempOld[5]  = tempWood;				// Node 6 is all of the Wood (joists, trusses, etc.) lumped together
			tempOld[6]  = tempCeiling;			// Node 7 is the Ceiling of the House
			tempOld[7]  = tempAtticFloor;		// Node 8 is the Floor of the Attic
			tempOld[8]  = tempInnerGable;		// Node 9 is the Inner Gable Wall (both lumped together)
			tempOld[9]  = tempOuterGable;		// Node 10 is the Outer Gable Wall (both lumped together)
			tempOld[10] = tempRetSurface;		// Node 11 is the Return Duct Outer Surface
			tempOld[11] = tempReturn;			// Node 12 is the Return Duct Air
			tempOld[12] = tempHouseMass;		// Node 13 is The Mass of the House
			tempOld[13] = tempSupSurface;		// Node 14 is the Supply Duct Outer Surface
			tempOld[14] = tempSupply;			// Node 15 is the Supply Duct Air
			tempOld[15] = tempHouse;			// Node 16 is the House Air (all one zone)


			
			

			// ************** house ventilation rate  - what would be measured with a tracer gas i.e., not just envelope and vent fan flows
			// mIN has msupreg added in mass balance calculations and mRetLeak contributes to house ventilation rate

			mHouse = mIN - mRetLeak * (1 - supLF) - mSupReg; //Brennan, I think this is what we should use. It's already calculated! For this to equal 0, either all values evaluate to 0, or mIN exactly equals the sum of the other values, first seems likely.
			mHouse = mHouse - mFanCycler - mERV_AH - mHRV_AH;

			qHouse = abs(mHouse / airDensityIN);			// Air flow rate through the house [m^3/s]. Brennan. These suddenly compute to 0, at minute 228,044, which means mHouse went to 0. I think. We get very werid flucations in house pressure and qHouse leading up to the error, where pressure cycles minute-by-minute between 4 and almost 0, and then eventually evaluates to actual 0.
			houseACH = qHouse / houseVolume * 3600;			// Air Changes per Hour for the whole house [h-1]. Brennan. These suddnely compute to 0, at minute 228,044

			if(mFlue < 0)												// mFlue is the mass airflow through all of the flues combined (if any)
				flueACH = (mFlue / airDensityIN) / houseVolume * 3600;	// Flow from house air to outside
			else
				flueACH = (mFlue / airDensityOUT) / houseVolume * 3600;	// Flow from outside to inside


			//// ************** Brennan Less. Calculating the dry and moist air loads due to air exchange.  
            if(mCeiling >= 0) { // flow from attic to house. Brennan, create new variable. Include envelope, ceiling and air handler off flows, but NOT mSupReg or mRetReg
			//mHouseIN = mIN - mCeiling - mSupReg - mSupAHoff - mRetAHoff; //Above, all these things have been added into mIN or mOUT, depending on flow directions.
			mCeilingIN = mCeiling + mSupAHoff + mRetAHoff; //Do we want to include duct leakage in ventilation loads? If so, we need another term, including supply/reutrn temps. 
			//mHouseOUT = mOUT - mRetReg; //Why do we add them in above and then subtract them out here. I DO NOT understand. 
		} else {
			//mHouseIN = mIN - mSupReg;
			mCeilingIN = 0;
			//mHouseOUT = mOUT - mCeiling - mRetReg - mSupAHoff - mRetAHoff;
		} //Yihuan:add these calculation for mCeilingIN calculation 

			//Calculation of dry and moist air ventilation loads. Currently all values are positive, ie don't differentiate between heat gains and losses. Load doens't necessarily mean energy use...
			Dhda = 1.006 * abs(tempHouse - tempOut); // dry air enthalpy difference across non-ceiling envelope elements [kJ/kg].
			ceilingDhda = 1.006 * abs(tempHouse - tempAttic); //Dry air enthalpy difference across ceiling [kJ/kg]
			Dhma = abs((1.006 * (tempHouse - tempOut)) + ((HR[3] * (2501 + 1.86 * tempHouse))-(HROUT * (2501 + 1.86 * tempOut)))); //moist air enthalpy difference across non-ceiling envelope elements [kJ/kg]
			ceilingDhma = abs((1.006 * (tempHouse - tempAttic)) + ((HR[3] * (2501 + 1.86 * tempHouse)) - (HR[0] * (2501 + 1.86 * tempAttic)))); //moist air enthalpy difference across ceiling [kJ/kg]


			DAventLoad = (mHouseIN * Dhda) + (mCeilingIN * ceilingDhda); //[kJ/min], I prevoiusly used mHouseIN, but changed to mHouse. This is super simplified, in that it does not account for differing dT across ducts, ceiling, walls, etc. these assume balanced mass flow (mHouseIN=mHouseOUT), I had assumed a need to multiply by 60 to convert kg/s kg/m, but that was not necessary once results were available, so I removed it.
			MAventLoad = (mHouseIN * Dhma) + (mCeilingIN * ceilingDhma); //[kJ/min]. Brennan, mHouseIN needs to be a new variable that we create in the functions.cpp tab.

			TotalDAventLoad = TotalDAventLoad + DAventLoad; //sums the dry air loads
			TotalMAventLoad = TotalMAventLoad + MAventLoad; //sums the moist air loads

			// [END] Heat and Mass Transport ==================================================================================================================================

			// [START] IAQ Calculations =======================================================================================================================================
			if(ventSumIN > ventSumOUT)						//ventSum based on largest of inflow or outflow (includes flue flows as default)
				ventSum = ventSumIN + abs(flueACH);
			else
				ventSum = ventSumOUT + abs(flueACH);

			if(ventSum <= 0)
				ventSum = .000001;

			/* -------dkm: To add flue in relDose/relExp calc the following two IF sentences have been added------
			if(flueACH <= 0)
			ventSumOUTD = ventSumOUT + abs(flueACH);
			else
			ventSumIND = ventSumIN + flueACH;

			if(ventSumIND > ventSumOUTD)	//ventSum based on largest of inflow or outflow
			ventsumD = ventSumIND;
			else
			ventsumD = ventSumOUTD;

			if(ventsumD <= 0)
			ventsumD = .000001;*/

			//Calculation of turnover
			// Automatically chosen based on previous selection of Aeq calculations (above) using AeqCalcs variable. Brennan. Need to change. 
			switch (AeqCalcs) {
			case 1:
				// 1. 62.2 ventilation rate with no infiltration credit
				turnover = (1 - exp(-(ventSum + wInfil) * rivecdt)) / (ventSum + wInfil) + turnoverOld * exp(-(ventSum + wInfil) * rivecdt); //This is used to compare Qtot (i.e., AEQ) to ventSum+wInfil
				//turnover = (1 - exp(-ventSum * rivecdt)) / ventSum + turnoverOld * exp(-ventSum * rivecdt); //This was the orignal formulation here. Assumes that ONLY fan flow is known for IAQ calculations. I had to change this based on use of AeqCalcs earlier in the code.
				break;
			case 2:
				// 2. 62.2 ventilation rate + infiltration credit from weather factors (w x NL)
				turnover = (1 - exp(-(ventSum + wInfil) * rivecdt)) / (ventSum + wInfil) + turnoverOld * exp(-(ventSum + wInfil) * rivecdt);
				break;
			case 3:
				// 3. 62.2 ventilation rate + 62.2 default infiltration credit (62.2, 4.1.3 p.4)
				turnover = (1 - exp(-(ventSum + defaultInfil) * rivecdt)) / (ventSum + defaultInfil) + turnoverOld * exp(-(ventSum + defaultInfil) * rivecdt);
				//turnover = (1 - exp(-(houseACH) * rivecdt)) / (houseACH) + turnoverOld * exp(-(houseACH) * rivecdt);
				break;
			case 4:
				// 1. 62.2 ventilation rate with no infiltration credit + existing home deficit. This is the same as the original formulation for AeqCalcs =1 (commented out above). Again, this is just deal with the use of AeqCalcs variable earlier in the codes.
				turnover = (1 - exp(-ventSum * rivecdt)) / ventSum + turnoverOld * exp(-ventSum * rivecdt);
				break;
			}

			// The 'real' turnover of the house using ACH of the house (that includes infiltration rather than just mechanical ventilation and flues)
			turnoverReal = (1 - exp(-(houseACH) * rivecdt)) / (houseACH) + turnoverRealOld * exp(-(houseACH) * rivecdt);
			relExp = Aeq * turnover; 
			relExpReal = Aeq * turnoverReal;

			// Relative Dose and Exposure for times when the house is occupied
			if(occupied[HOUR] == 0) { //unoccupied. fixes dose at 1
				relDose = 1 * (1 - exp(-rivecdt / 24)) + relDoseOld * exp(-rivecdt / 24);
				relDoseReal = 1 * (1 - exp(-rivecdt / 24)) + relDoseRealOld * exp(-rivecdt / 24);
			} else { //occupied. dose calculated by based on turnover.
				relDose = relExp * (1 - exp(-rivecdt / 24)) + relDoseOld * exp(-rivecdt / 24);
				relDoseReal = relExpReal * (1 - exp(-rivecdt / 24)) + relDoseRealOld * exp(-rivecdt / 24);
			}
			
			occupiedDose = relDose * occupied[HOUR];				// dose and exposure for when the building is occupied
			occupiedExp = relExp * occupied[HOUR];					//If house is always occupied, then relExp = occupiedExp
			
			occupiedDoseReal = relDoseReal * occupied[HOUR];		// dose and exposure for when the building is occupied
			occupiedExpReal = relExpReal * occupied[HOUR];			//If house is always occupied, then relExpReal = occupiedExpReal

			if(occupied[HOUR] == 1) {
				occupiedMinCount = occupiedMinCount + 1;			// counts number of minutes while house is occupied
			}

			totalOccupiedDose = totalOccupiedDose + occupiedDose;   // total dose and exp over the occupied time period
			totalOccupiedExp = totalOccupiedExp + occupiedExp;

			totalOccupiedDoseReal = totalOccupiedDoseReal + occupiedDoseReal;   // Brennan. Added these for "real" calculations. total dose and exp over the occupied time period
			totalOccupiedExpReal = totalOccupiedExpReal + occupiedExpReal;
			
			meanRelDose = meanRelDose + relDose;
			meanRelExp = meanRelExp + relExp;

			meanRelDoseReal = meanRelDoseReal + relDoseReal; //Brennan, added these to include REAL calculations.
			meanRelExpReal = meanRelExpReal + relExpReal;

		




			// [END] IAQ calculations =======================================================================================================================================

			//Calculation of house relative humidity, as ratio of Partial vapor pressure (calculated in rh variable below) to the Saturation vapor pressure (calculated above). 
			
			RHhouse = 100 * ((pRef*(HR[3]/0.621945))/(1+(HR[3]/0.621945)) / SatVaporPressure);

			//RHhouse = 100 * (pRef*(HR[3]/0.621945))/(1+(HR[3]/0.621945)) / (exp(77.345+(0.0057*tempHouse)-(7235/tempHouse))/(pow(tempHouse,8.2))); //This was Brennan's old rh calculation (used during smart ventilatoin humidity control), which was correct to within ~0.01% or less at possible house temperatures 15-30C. 

			if(RHhouse >= 60) //RHind60 and 70 count the minutes where high RH occurs
				RHind60 = 1;
			else
				RHind60 = 0;

			if(RHhouse >= 70)
				RHind70 = 1;
			else
				RHind70 = 0;

			RHtot60 = RHtot60 + RHind60; //These are summed for the year and summarized in the output file. 
			RHtot70 = RHtot70 + RHind70;
			

			// Writing results to a csv file
			double setpoint;

			if(hcFlag == 1)
				setpoint = heatThermostat[HOUR];
			else
				setpoint = coolThermostat[HOUR];

			// ================================= WRITING RCO DATA FILE =================================

			//File column names, for reference.
			//outputFile << "Time\tMin\twindSpeed\ttempOut\ttempHouse\tsetpoint\ttempAttic\ttempSupply\ttempReturn\tAHflag\tAHpower\tHcap\tcompressPower\tCcap\tmechVentPower\tHR\tSHR\tMcoil\thousePress\tQhouse\tACH\tACHflue\tventSum\tnonRivecVentSum\tfan1\tfan2\tfan3\tfan4\tfan5\tfan6\tfan7\trivecOn\tturnover\trelExpRIVEC\trelDoseRIVEC\toccupiedExpReal\toccupiedDoseReal\toccupied\toccupiedExp\toccupiedDose\tDAventLoad\tMAventLoad\tHROUT\tHRhouse\tRH%house\tRHind60\tRHind70" << endl; 

			// tab separated instead of commas- makes output files smaller
			outputFile << HOUR << "\t" << MINUTE << "\t" << windSpeed << "\t" << tempOut << "\t" << tempHouse << "\t" << setpoint << "\t";
			outputFile << tempAttic << "\t" << tempSupply << "\t" << tempReturn << "\t" << AHflag << "\t" << AHfanPower << "\t";
			outputFile << hcap << "\t" << compressorPower << "\t" << capacityc << "\t" << mechVentPower << "\t" << HR[3] * 1000 << "\t" << SHR << "\t" << Mcoil << "\t";
			outputFile << Pint << "\t"<< qHouse << "\t" << houseACH << "\t" << flueACH << "\t" << ventSum << "\t" << nonRivecVentSum << "\t";
			outputFile << fan[0].on << "\t" << fan[1].on << "\t" << fan[2].on << "\t" << fan[3].on << "\t" << fan[4].on << "\t" << fan[5].on << "\t" << fan[6].on << "\t" ;
			outputFile << rivecOn << "\t" << turnover << "\t" << relExp << "\t" << relDose << "\t" << occupiedExpReal << "\t" << occupiedDoseReal << "\t";
			outputFile << occupied[HOUR] << "\t" << occupiedExp << "\t" << occupiedDose << "\t" << DAventLoad << "\t" << MAventLoad << "\t" << HROUT << "\t" << HR[3] << "\t" << RHhouse << "\t" << RHind60 << "\t" << RHind70 << endl; //<< "\t" << mIN << "\t" << mOUT << "\t" ; //Brennan. Added DAventLoad and MAventLoad and humidity values.
			//outputFile << mCeiling << "\t" << mHouseIN << "\t" << mHouseOUT << "\t" << mSupReg << "\t" << mRetReg << "\t" << mSupAHoff << "\t" ;
			//outputFile << mRetAHoff << "\t" << mHouse << "\t"<< flag << "\t"<< AIM2 << "\t" << AEQaim2FlowDiff << "\t" << qFanFlowRatio << "\t" << C << endl; //Breann/Yihuan added these for troubleshooting

			// ================================= WRITING MOISTURE DATA FILE =================================
			//File column names, for reference.
			//moistureFile << "HROUT\tHRattic\tHRreturn\tHRsupply\tHRhouse\tHRmaterials\tRH%house\tRHind60\tRHind70" << endl;

			moistureFile << HROUT << "\t" << HR[0] << "\t" << HR[1] << "\t" << HR[2] << "\t" << HR[3] << "\t" << HR[4] << "\t" << RHhouse << "\t" << RHind60 << "\t" << RHind70 << endl;

			// ================================= WRITING Filter Loading DATA FILE =================================
			
			massFilter_cumulative = massFilter_cumulative + (mAH * 60);	// Time steps are every minute and mAH is in [kg/s]
			massAH_cumulative = massAH_cumulative + (mAH * 60);			// Time steps are every minute and mAH is in [kg/s]
			

			// Filter loading output file
			filterFile << massAH_cumulative << "\t"  << qAH << "\t" << AHfanPower << "\t" << retLF << endl;
			
			// Calculating sums for electrical and gas energy use
			AH_kWh = AH_kWh + AHfanPower / 60000;						// Total air Handler energy for the simulation in kWh
			compressor_kWh = compressor_kWh + compressorPower / 60000;	// Total cooling/compressor energy for the simulation in kWh
			mechVent_kWh = mechVent_kWh + mechVentPower / 60000;		// Total mechanical ventilation energy for over the simulation in kWh
			gasTherm = gasTherm + hcap * 60 / 1000000 / 105.5;			// Total Heating energy for the simulation in therms
			furnace_kWh = gasTherm * 29.3;								// Total heating/furnace energy for the simulation in kWh

			// Average temperatures and airflows
			meanOutsideTemp = meanOutsideTemp + (tempOut - 273.15);		// Average external temperature over the simulation
			meanAtticTemp = meanAtticTemp + (tempAttic -273.15);		// Average attic temperatire over the simulation
			meanHouseTemp = meanHouseTemp + (tempHouse - 273.15);		// Average internal house temperature over the simulation
			meanHouseACH = meanHouseACH + houseACH;						// Average ACH for the house
			meanFlueACH = meanFlueACH + abs(flueACH);					// Average ACH for the flue (-ve and +ve flow considered useful)

			// Time keeping
			MINUTE++;													// Minute count of year
			minute_day++;												// Minute count (of day so 1 to 60)
			//timeSteps++;												// Simulation time steps

			if(MINUTE > (totaldays * 1440))
				break;

		} while (weatherFile);			// Run until end of weather file
		//} while (day <= totaldays);	// Run for one calendar year

		//[END] Main Simulation Loop ==============================================================================================================================================

		//------------Simulation Start and End Times----------
		time(&endTime);
		string runEndTime = ctime(&endTime);

		cout << "\nStart of simulations\t= " << runStartTime;
		cout << "End of simulations\t= " << runEndTime << endl;
		//----------------------------------------------------

		// Close files
		outputFile.close();
		weatherFile.close();
		moistureFile.close();
		filterFile.close();

		if(dynamicScheduleFlag == 1)								// Fan Schedule
			fanschedulefile.close();

		double total_kWh = AH_kWh + furnace_kWh + compressor_kWh + mechVent_kWh;

		meanOutsideTemp = meanOutsideTemp / MINUTE;
		meanAtticTemp = meanAtticTemp / MINUTE;
		meanHouseTemp = meanHouseTemp / MINUTE;
		meanHouseACH = meanHouseACH / MINUTE;
		meanFlueACH = meanFlueACH / MINUTE;
		meanRelDose = meanRelDose / MINUTE;
		meanRelExp = meanRelExp / MINUTE;
		meanRelDoseReal = meanRelDoseReal / MINUTE;				//Brennan. Added these and need to define in the definitions area. 
		meanRelExpReal = meanRelExpReal / MINUTE;

		meanOccupiedDose = totalOccupiedDose / occupiedMinCount;
		meanOccupiedExp = totalOccupiedExp / occupiedMinCount;

		meanOccupiedDoseReal = totalOccupiedDoseReal / occupiedMinCount;
		meanOccupiedExpReal = totalOccupiedExpReal / occupiedMinCount;

		RHexcAnnual60 = RHtot60 / MINUTE;
		RHexcAnnual70 = RHtot70 / MINUTE;
		
		// Write summary output file (RC2 file)
		ofstream ou2File(outPath + output_file + ".rc2"); 
		if(!ou2File) { 
			cout << "Cannot open: " << outPath + output_file + ".rc2" << endl;
			system("pause");
			return 1; 
		}

			// ================================= WRITING ANNUAL SUMMAR (.RC2) DATA FILE =================================

		//Column header names
		ou2File << "Temp_out\tTemp_attic\tTemp_house";
		ou2File << "\tAH_kWh\tfurnace_kWh\tcompressor_kWh\tmechVent_kWh\ttotal_kWh\tmean_ACH\tflue_ACH";
		ou2File << "\tmeanRelExpReal\tmeanRelDoseReal\tmeanOccupiedExpReal\tmeanOccupiedDoseReal";
		ou2File << "\toccupiedMinCount\trivecMinutes\tNL\tC\tAeq\tfilterChanges\tMERV\tloadingRate\tDryAirVentLoad\tMoistAirVentLoad\tRHexcAnnual60\tRHexcAnnual70" << endl;
		//Values
		ou2File << meanOutsideTemp << "\t" << meanAtticTemp << "\t" << meanHouseTemp << "\t";
		ou2File << AH_kWh << "\t" << furnace_kWh << "\t" << compressor_kWh << "\t" << mechVent_kWh << "\t" << total_kWh << "\t" << meanHouseACH << "\t" << meanFlueACH << "\t";
		ou2File << meanRelExp << "\t" << meanRelDose << "\t" << meanOccupiedExpReal << "\t" << meanOccupiedDoseReal << "\t"; //Brennan. changed all the exp/dose outputs to be "real"
		ou2File << occupiedMinCount << "\t" << rivecMinutes << "\t" << NL << "\t" << C << "\t" << Aeq << "\t" << filterChanges << "\t" << MERV << "\t" << loadingRate << "\t" << TotalDAventLoad << "\t" << TotalMAventLoad << "\t" << RHexcAnnual60 << "\t" << RHexcAnnual70 << endl;

		ou2File.close();

	}

	system("pause");

	return 0;
}
