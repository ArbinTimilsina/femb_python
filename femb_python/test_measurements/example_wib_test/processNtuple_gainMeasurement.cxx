//compile independently with: g++ -std=c++11 -o processNtuple_gainMeasurement processNtuple_gainMeasurement.cxx `root-config --cflags --glibs`
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
using namespace std;

#include "TROOT.h"
#include "TMath.h"
#include "TApplication.h"
#include "TFile.h"
#include "TTree.h"
#include "TH1.h"
#include "TH2.h"
#include "TString.h"
#include "TCanvas.h"
#include "TSystem.h"
#include "TGraph.h"
#include "TProfile2D.h"
#include "TF1.h"

using namespace std;

//global TApplication object declared here for simplicity
TApplication *theApp;

class Analyze {
	public:
	Analyze(std::string inputFileName);
        int processFileName(std::string inputFileName, std::string &baseFileName);
	void doAnalysis();
	void analyzeChannel();
	void findPulses();
	void analyzePulse(int startSampleNum);
	void measurePulseHeights();
	void measureGain();

	//Files
	TFile* inputFile;
	TFile *gOut;

	//ROI tr_rawdata variables
	TTree *tr_rawdata;
	unsigned short subrun, chan;
	std::vector<unsigned short> *wf = 0;

	//Constants
	const int numChan = 128;// 35t
	int preRange = 20;
	int postRange = 20;
	const float SAMP_PERIOD = 0.5; //us
	const int numSubrun = 32;

	//data objects
	TCanvas* c0;
	TGraph *gCh;

	//Pulse height measurement	
	std::vector<int> pulseStart;
	TH1F *hPulseHeights;

	//RMS measurement
	double chanRms;

	//Gain Measurement
	TGraph *gPulseVsSignal[128];
	double signalSizes[64] = {0.606,0.625,0.644,0.663,0.682,0.701,0.720,0.739,0.758,0.777,0.796,0.815,0.834,
		0.853,0.872,0.891,0.909,0.928,0.947,0.966,0.985,1.004,1.023,1.042,1.061,1.080,1.099,1.118,1.137,
		1.156,1.175,1.194,1.213,1.232,1.251,1.269,1.288,1.307,1.326,1.345,1.364,1.383,1.402,1.421,1.440,
		1.459,1.478, 1.497,1.516,1.535,1.554,1.573,1.592,1.611,1.629,1.648,1.667,1.686,1.705,1.724,1.743,
		1.762,1.781,1.800};

	TH1F *hRmsVsChan = new TH1F("hRmsVsChan","",128,0-0.5,128-0.5);
	TH1F *hGainVsChan = new TH1F("hGainVsChan","",128,0-0.5,128-0.5);
	TH1F *hEncVsChan = new TH1F("hEncVsChan","",128,0-0.5,128-0.5);
};

Analyze::Analyze(std::string inputFileName){

	//get input file
	if( inputFileName.empty() ){
		std::cout << "Error invalid file name" << std::endl;
		gSystem->Exit(0);
	}

	inputFile = new TFile(inputFileName.c_str());
	if (inputFile->IsZombie()) {
		std::cout << "Error opening input file" << std::endl;
		gSystem->Exit(0);
	}

	if( !inputFile ){
		std::cout << "Error opening input file" << std::endl;
		gSystem->Exit(0);
	}

	//initialize tr_rawdata branches
  	tr_rawdata = (TTree*) inputFile->Get("femb_wfdata");
  	if( !tr_rawdata ){
		std::cout << "Error opening input file tree" << std::endl;
		gSystem->Exit(0);
  	}
	tr_rawdata->SetBranchAddress("subrun", &subrun);
	tr_rawdata->SetBranchAddress("chan", &chan);
  	tr_rawdata->SetBranchAddress("wf", &wf);

	//make output file
  	std::string outputFileName;
	if( processFileName( inputFileName, outputFileName ) )
		outputFileName = "output_processNtuple_" + outputFileName;
	else
		outputFileName = "output_processNtuple.root";

  	gOut = new TFile(outputFileName.c_str() , "RECREATE");

  	//initialize canvas
  	c0 = new TCanvas("c0", "c0",1400,800);

	//initialize graphs
	gCh = new TGraph();

	//gain measurement objects
	hPulseHeights = new TH1F("hPulseHeights","",4100,0-0.5,4100-0.5);
	for(int ch = 0 ; ch < numChan ; ch++ ){
		gPulseVsSignal[ch] = new TGraph();
	}
}

int Analyze::processFileName(std::string inputFileName, std::string &baseFileName){
        //check if filename is empty
        if( inputFileName.size() == 0 ){
                std::cout << "processFileName : Invalid filename " << std::endl;
                return 0;
        }

        //remove path from name
        size_t pos = 0;
        std::string delimiter = "/";
        while ((pos = inputFileName.find(delimiter)) != std::string::npos)
                inputFileName.erase(0, pos + delimiter.length());

	if( inputFileName.size() == 0 ){
                std::cout << "processFileName : Invalid filename " << std::endl;
                return 0;
        }

        //replace / with _
        std::replace( inputFileName.begin(), inputFileName.end(), '/', '_'); // replace all 'x' to 'y'
        std::replace( inputFileName.begin(), inputFileName.end(), '-', '_'); // replace all 'x' to 'y'

	baseFileName = inputFileName;
	
	return 1;
}

void Analyze::doAnalysis(){
  	//loop over tr_rawdata entries
  	Long64_t nEntries(tr_rawdata->GetEntries());

	tr_rawdata->GetEntry(0);
	//loop over pulse waveform
	for(Long64_t entry(0); entry<nEntries; ++entry) { 
		tr_rawdata->GetEntry(entry);

		//make sure channels and subruns are ok
		if( chan < 0 || chan >= numChan ) continue;
		if( subrun < 0 || subrun >= numSubrun ) continue;

		//analyze current entry - 1 channel
    		analyzeChannel();

		if( subrun == 0 )
			hRmsVsChan->SetBinContent(chan+1, chanRms);

		//find pulses in current channel
		findPulses();

		//analyze channel pulses, measure heights
		hPulseHeights->Reset();
		for( unsigned int p = 0 ; p < pulseStart.size() ; p++ )
			analyzePulse( pulseStart.at(p) ) ;
		measurePulseHeights();
  	}//entries

	measureGain();

 	gOut->Cd("");

	hGainVsChan->GetXaxis()->SetTitle("Channel #");
	hGainVsChan->GetYaxis()->SetTitle("Gain (e- / ADC count)");
	hGainVsChan->Write();

	hRmsVsChan->GetXaxis()->SetTitle("Channel #");
	hRmsVsChan->GetYaxis()->SetTitle("Pedestal RMS (ADC counts)");
	hRmsVsChan->Write();

	hEncVsChan->GetXaxis()->SetTitle("Channel #");
	hEncVsChan->GetYaxis()->SetTitle("ENC (e-)");
	hEncVsChan->Write();

	for(int ch = 0 ; ch < numChan ; ch++ ){
		std::string title = "gPulseHeightVsSignal_Ch_" + to_string( ch );
		gPulseVsSignal[ch]->GetXaxis()->SetTitle("Number of Electrons (e-)");
		gPulseVsSignal[ch]->GetYaxis()->SetTitle("Measured Pulse Height (ADC counts)");
		gPulseVsSignal[ch]->Write(title.c_str());
	}
	
  	gOut->Close();
}

void Analyze::analyzeChannel(){

	 //skip known bad channels here

	//calculate mean
	double mean = 0;
	int count = 0;
	for( int s = 0 ; s < wf->size() ; s++ ){
		if(  wf->at(s) < 10 ) continue;
		if( (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		double value = wf->at(s);
		mean += value;
		count++;
	}
	if( count > 0 )
		mean = mean / (double) count;

	//calculate rms
	double rms = 0;
	count = 0;
	for( int s = 0 ; s < wf->size() ; s++ ){
		if(  wf->at(s) < 10 ) continue;
		if( (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		double value = wf->at(s);
		rms += (value-mean)*(value-mean);
		count++;
	}	
	if( count > 1 )
		rms = TMath::Sqrt( rms / (double)( count - 1 ) );

	//load hits into TGraph, skip stuck codes
	gCh->Set(0);
	for( int s = 0 ; s < wf->size() ; s++ ){
		if(  wf->at(s) < 10 ) continue;
		if( (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		gCh->SetPoint(gCh->GetN() , s , wf->at(s) );
	}
	
	//compute FFT - use TGraph to interpolate between missing samples
	//int numFftBins = wf->size();
	int numFftBins = 500;
	if( numFftBins > wf->size() )
		numFftBins = wf->size();
	TH1F *hData = new TH1F("hData","",numFftBins,0,numFftBins);
	for( int s = 0 ; s < numFftBins ; s++ ){
		double adc = gCh->Eval(s);
		hData->SetBinContent(s+1,adc);
	}

	TH1F *hFftData = new TH1F("hFftData","",numFftBins,0,numFftBins);
    	hData->FFT(hFftData,"MAG");
    	for(int i = 1 ; i < hFftData->GetNbinsX() ; i++ ){
		double freq = 2.* i / (double) hFftData->GetNbinsX() ;
	}	

	//selection here
	chanRms = rms;

	//draw waveform if wanted
	if( 0 ){
		gCh->Set(0);
		for( int s = 0 ; s < wf->size() ; s++ )
			gCh->SetPoint(gCh->GetN() , gCh->GetN() , wf->at(s) );
		std::cout << "Channel " << chan << std::endl;
		c0->Clear();
		std::string title = "Subrun " + to_string( subrun ) + " Channel " + to_string( chan );
		gCh->SetTitle( title.c_str() );
		gCh->GetXaxis()->SetTitle("Sample Number");
		gCh->GetYaxis()->SetTitle("Sample Value (ADC counts)");
		//gCh->GetXaxis()->SetRangeUser(0,128);
		//gCh->GetXaxis()->SetRangeUser(0,num);
		//gCh->GetYaxis()->SetRangeUser(500,1000);
		gCh->Draw("ALP");
		/*
		c0->Divide(2);
		c0->cd(1);
		hData->Draw();
		c0->cd(2);
		hFftData->SetBinContent(1,0);
		hFftData->GetXaxis()->SetRangeUser(0, hFftData->GetNbinsX()/2. );
		hFftData->Draw();
		*/
		c0->Update();
		char ct;
		std::cin >> ct;
		usleep(1000);
	}

	delete hData;
	delete hFftData;
}

void Analyze::findPulses(){
	//calculate mean
	double mean = 0;
	int count = 0;
	for( int s = 0 ; s < wf->size() ; s++ ){
		if(  (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		double value = wf->at(s);
		mean += value;
		count++;
	}
	if( count > 0 )
		mean = mean / (double) count;

	//calculate rms
	double rms = 0;
	count = 0;
	for( int s = 0 ; s < wf->size() ; s++ ){
		if(  (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		double value = wf->at(s);
		rms += (value-mean)*(value-mean);
		count++;
	}	
	if( count > 1 )
		rms = TMath::Sqrt( rms / (double)( count - 1 ) );

	//calculate RMS from histogram

	//look for pulses along waveform, hardcoded number might be bad
	double threshold = 5*rms;
	if( threshold > 50 )
		threshold = 50.;
	int numPulse = 0;
	pulseStart.clear();
	for( int s = 0 + preRange ; s < wf->size() - postRange - 1 ; s++ ){
		if(  (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		if(  (wf->at(s+1) & 0x3F ) == 0x0 || (wf->at(s+1) & 0x3F ) == 0x3F ) continue;
		double value =  wf->at(s);
		double valueNext = wf->at(s+1);
		if(1 && valueNext > mean + threshold && value < mean + threshold ){
			//have pulse, find local maxima
			numPulse++;
			int start = s;
			pulseStart.push_back(start );
		}
	}

	//draw waveform if wanted
	if( 0 ){
		gCh->Set(0);
		for( int s = 0 ; s < wf->size() ; s++ )
			gCh->SetPoint(gCh->GetN() , gCh->GetN() , wf->at(s) );
		std::cout << "Channel " << chan << std::endl;
		c0->Clear();
		std::string title = "Subrun " + to_string( subrun ) + " Channel " + to_string( chan );
		gCh->SetTitle( title.c_str() );
		gCh->GetXaxis()->SetTitle("Sample Number");
		gCh->GetYaxis()->SetTitle("Sample Value (ADC counts)");
		gCh->Draw("ALP");
		c0->Update();
		//char ct;
		//std::cin >> ct;
		usleep(100000);
	}
}

void Analyze::analyzePulse(int startSampleNum){
	//require pulse is not beside waveform edge
	if( startSampleNum <= preRange || startSampleNum >= wf->size()  - postRange )
		return;

	//calculate baseline estimate in range preceeding pulse
	double mean = 0;
	int count = 0;
	for(int s = startSampleNum-20 ; s < startSampleNum - 10 ; s++){
		if( s < 0 ) continue;
		if( s >= wf->size() ) continue;
		if( (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		double value = wf->at(s);
		mean += value;
		count++;
	}
	if( count > 0)
		mean = mean / (double) count;
	if( count == 0 )
		return;

	//calculate baseline rms in range preceeding pulse
	double rms = 0;
	count = 0;
	for(int s = startSampleNum-20 ; s < startSampleNum - 10 ; s++){
		if( s < 0 ) continue;
		if( s >= wf->size() ) continue;
		if( (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		double value = wf->at(s);
		rms += (value - mean)*(value - mean);
		count++;
	}
	if(count - 1 > 0)
		rms = TMath::Sqrt( rms /(double)(  count - 1  ) );
	if( count == 0 )
		return;

	//find maximum sample value
	int maxSampTime = -1;
	int maxSampVal = -1;
	int maxSamp = -1;
	for(int s = startSampleNum-preRange ; s < startSampleNum + postRange ; s++){
		if( s < 0 ) continue;
		if( s >= wf->size() ) continue;
		if( (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		double value = wf->at(s);

		if( s < startSampleNum - 5 ) continue;
		if( s > startSampleNum + 10 ) continue;
		if( value > maxSampVal ){
			maxSampTime = s*SAMP_PERIOD;
			maxSampVal = value;
			maxSamp = s;
		}
	}

	//load pulse into graph object, do NOT include stuck codes, convert sample number to time (us)
	gCh->Set(0);
	for(int s = startSampleNum-preRange ; s < startSampleNum + postRange ; s++){
		if( s < 0 ) continue;
		if( s >= wf->size() ) continue;
		if( (wf->at(s) & 0x3F ) == 0x0 || (wf->at(s) & 0x3F ) == 0x3F ) continue;
		gCh->SetPoint(gCh->GetN() , s*SAMP_PERIOD , wf->at(s) );
	}

	//integrate over expected pulse range, note use of averaged waveform pulse time
	double sumVal = 0;
	for( int s = 0 ; s < gCh->GetN() ; s++ ){
		double sampTime,sampVal;
		gCh->GetPoint(s,sampTime,sampVal);
		sumVal += sampVal - mean;
	}

	//selection criteria
	

	//update histograms
	double pulseHeight = maxSampVal - mean;
	hPulseHeights->Fill(pulseHeight);

	//draw waveform if needed
	if(0){
		std::cout << mean << "\t" << maxSampVal << "\t" << pulseHeight << std::endl;
		std::string title = "Subrun " + to_string( subrun ) + " Channel " + to_string( chan ) + " Height " + to_string( int(pulseHeight) );
		gCh->SetTitle( title.c_str() );

		gCh->GetXaxis()->SetTitle("Sample Number");
		gCh->GetYaxis()->SetTitle("Sample Value (ADC counts)");
	
		c0->Clear();
		gCh->SetMarkerStyle(21);
		gCh->SetMarkerColor(kRed);
		gCh->Draw("ALP");
		c0->Update();
		usleep(100000);
		char ct;
		std::cin >> ct;
	}
}

void Analyze::measurePulseHeights(){

	if( hPulseHeights->GetEntries() < 5 )
		return;

	//get average pulse height, update plots
	double mean = hPulseHeights->GetMean();
	double signalCharge = (signalSizes[subrun]-signalSizes[0])*183*6241;
	gPulseVsSignal[chan]->SetPoint( gPulseVsSignal[chan]->GetN() ,signalCharge , mean);
	
	if(0){
		std::cout << "mean " << mean << std::endl;
		std::string title = "Subrun " + to_string( subrun ) + ", Channel " + to_string( chan ) + ", e- " + to_string( int(signalCharge) ) 
			+ ", Height " + to_string( int(mean) );
		hPulseHeights->SetTitle( title.c_str() );

		double max = hPulseHeights->GetBinCenter( hPulseHeights->GetMaximumBin() ) ;

		hPulseHeights->GetXaxis()->SetRangeUser( max - 50 , max + 50 );
		hPulseHeights->GetXaxis()->SetTitle("Measured Pulse Height (ADC counts)");
		hPulseHeights->GetYaxis()->SetTitle("Number of Pulses");

		c0->Clear();
		hPulseHeights->Draw();
		c0->Update();
		usleep(100000);
		char ct;
		std::cin >> ct;
	}
	return;
}

void Analyze::measureGain(){
	for(int ch = 0 ; ch < numChan ; ch++ ){
		if( gPulseVsSignal[ch]->GetN() < 3 ) continue;

		//gPulseVsSignal[ch]->GetXaxis()->SetRangeUser(700*1000.,1400*1000.);
		gPulseVsSignal[ch]->GetXaxis()->SetRangeUser(0*1000.,700*1000.);

		TF1 *f1 = new TF1("f1","pol1",0*1000.,700*1000.);
		f1->SetParameter(0,0);
		f1->SetParameter(1,2/1000.);
		gPulseVsSignal[ch]->Fit("f1","QR");

		double gain_AdcPerE = f1->GetParameter(1);
		double gain_ePerAdc = 0;
		if( gain_AdcPerE > 0 )
			gain_ePerAdc = 1./ gain_AdcPerE;
		hGainVsChan->SetBinContent(ch+1, gain_ePerAdc);

		double enc = hRmsVsChan->GetBinContent(ch+1)*gain_ePerAdc;
		hEncVsChan->SetBinContent(ch+1, enc);
	
		if(0){
			std::cout << gain_ePerAdc << std::endl;
			c0->Clear();
			gPulseVsSignal[ch]->Draw("ALP");
			c0->Update();
			//char ct;
			//std::cin >> ct;
		}

		delete f1;
	}

	return;
}

void processNtuple(std::string inputFileName) {

  Analyze ana(inputFileName);
  ana.doAnalysis();

  return;
}

int main(int argc, char *argv[]){
  if(argc!=2){
    cout<<"Usage: processNtuple [inputFilename]"<<endl;
    return 0;
  }

  std::string inputFileName = argv[1];
  std::cout << "inputFileName " << inputFileName << std::endl;

  //define ROOT application object
  theApp = new TApplication("App", &argc, argv);
  processNtuple(inputFileName); 

  //return 1;
  gSystem->Exit(0);
}