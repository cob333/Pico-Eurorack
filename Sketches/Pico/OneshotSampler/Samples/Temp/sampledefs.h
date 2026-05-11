// sample structure built by wav2header based on wav2sketch by Paul Stoffregen

struct sample_t {
  const int16_t * samplearray; // pointer to sample array
  uint32_t samplesize; // size of the sample array
  uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
  uint8_t MIDINOTE;  // MIDI note on that plays this sample
  uint8_t play_volume; // play volume 0-127
  char sname[20];        // sample name
} sample[] = {

	Clackverbpulsar,	// pointer to sample array
	Clackverbpulsar_SIZE,	// size of the sample array
	Clackverbpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clackverbpulsar",	// sample name

	Bdsubbuzzpulsar,	// pointer to sample array
	Bdsubbuzzpulsar_SIZE,	// size of the sample array
	Bdsubbuzzpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bdsubbuzzpulsar",	// sample name

	Chclassicpulsar08,	// pointer to sample array
	Chclassicpulsar08_SIZE,	// size of the sample array
	Chclassicpulsar08_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chclassicpulsar08",	// sample name

	Snapverblitepulsar,	// pointer to sample array
	Snapverblitepulsar_SIZE,	// size of the sample array
	Snapverblitepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Snapverblitepulsar",	// sample name

	Sdspikepulsar,	// pointer to sample array
	Sdspikepulsar_SIZE,	// size of the sample array
	Sdspikepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Sdspikepulsar",	// sample name

	Bassharmonicdrivelopulsarg0,	// pointer to sample array
	Bassharmonicdrivelopulsarg0_SIZE,	// size of the sample array
	Bassharmonicdrivelopulsarg0_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bassharmonicdrivelo",	// sample name

	Basssyninsectpulsarg2,	// pointer to sample array
	Basssyninsectpulsarg2_SIZE,	// size of the sample array
	Basssyninsectpulsarg2_SIZE,	//sampleindex. if at end of sample array sound is not playing
	41,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Basssyninsectpulsar",	// sample name

	Ohsimplepulsar01,	// pointer to sample array
	Ohsimplepulsar01_SIZE,	// size of the sample array
	Ohsimplepulsar01_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Ohsimplepulsar01",	// sample name

	Bdplucklitepulsar,	// pointer to sample array
	Bdplucklitepulsar_SIZE,	// size of the sample array
	Bdplucklitepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	43,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bdplucklitepulsar",	// sample name

	Sddelaycrackpulsar,	// pointer to sample array
	Sddelaycrackpulsar_SIZE,	// size of the sample array
	Sddelaycrackpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	44,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Sddelaycrackpulsar",	// sample name

	Woodblockpulsar,	// pointer to sample array
	Woodblockpulsar_SIZE,	// size of the sample array
	Woodblockpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	45,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Woodblockpulsar",	// sample name

	Cymbaldustpulsar,	// pointer to sample array
	Cymbaldustpulsar_SIZE,	// size of the sample array
	Cymbaldustpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	46,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Cymbaldustpulsar",	// sample name

	Chclassicpulsar06,	// pointer to sample array
	Chclassicpulsar06_SIZE,	// size of the sample array
	Chclassicpulsar06_SIZE,	//sampleindex. if at end of sample array sound is not playing
	47,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chclassicpulsar06",	// sample name

	Chdelaylitepulsar,	// pointer to sample array
	Chdelaylitepulsar_SIZE,	// size of the sample array
	Chdelaylitepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	48,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chdelaylitepulsar",	// sample name

	Snapdrypulsar,	// pointer to sample array
	Snapdrypulsar_SIZE,	// size of the sample array
	Snapdrypulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	49,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Snapdrypulsar",	// sample name

	Bdverbdarkpulsar,	// pointer to sample array
	Bdverbdarkpulsar_SIZE,	// size of the sample array
	Bdverbdarkpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	50,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bdverbdarkpulsar",	// sample name

};
