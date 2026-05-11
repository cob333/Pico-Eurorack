// sample structure built by wav2header based on wav2sketch by Paul Stoffregen

struct sample_t {
  const int16_t * samplearray; // pointer to sample array
  uint32_t samplesize; // size of the sample array
  uint32_t sampleindex; // current sample array index when playing. index at last sample= not playing
  uint8_t MIDINOTE;  // MIDI note on that plays this sample
  uint8_t play_volume; // play volume 0-127
  char sname[20];        // sample name
} sample[] = {

	Percxax,	// pointer to sample array
	Percxax_SIZE,	// size of the sample array
	Percxax_SIZE,	//sampleindex. if at end of sample array sound is not playing
	35,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Percxax",	// sample name

	Clackverbpulsar,	// pointer to sample array
	Clackverbpulsar_SIZE,	// size of the sample array
	Clackverbpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	36,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clackverbpulsar",	// sample name

	Percrodeo,	// pointer to sample array
	Percrodeo_SIZE,	// size of the sample array
	Percrodeo_SIZE,	//sampleindex. if at end of sample array sound is not playing
	37,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Percrodeo",	// sample name

	Bdsubbuzzpulsar,	// pointer to sample array
	Bdsubbuzzpulsar_SIZE,	// size of the sample array
	Bdsubbuzzpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	38,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bdsubbuzzpulsar",	// sample name

	Chclassicpulsar08,	// pointer to sample array
	Chclassicpulsar08_SIZE,	// size of the sample array
	Chclassicpulsar08_SIZE,	//sampleindex. if at end of sample array sound is not playing
	39,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chclassicpulsar08",	// sample name

	Openhatpbsadults,	// pointer to sample array
	Openhatpbsadults_SIZE,	// size of the sample array
	Openhatpbsadults_SIZE,	//sampleindex. if at end of sample array sound is not playing
	40,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Openhatpbsadults",	// sample name

	Hihatsnow,	// pointer to sample array
	Hihatsnow_SIZE,	// size of the sample array
	Hihatsnow_SIZE,	//sampleindex. if at end of sample array sound is not playing
	41,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Hihatsnow",	// sample name

	Snapverblitepulsar,	// pointer to sample array
	Snapverblitepulsar_SIZE,	// size of the sample array
	Snapverblitepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	42,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Snapverblitepulsar",	// sample name

	Sdspikepulsar,	// pointer to sample array
	Sdspikepulsar_SIZE,	// size of the sample array
	Sdspikepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	43,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Sdspikepulsar",	// sample name

	Percflick,	// pointer to sample array
	Percflick_SIZE,	// size of the sample array
	Percflick_SIZE,	//sampleindex. if at end of sample array sound is not playing
	44,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Percflick",	// sample name

	Perc16cowbell,	// pointer to sample array
	Perc16cowbell_SIZE,	// size of the sample array
	Perc16cowbell_SIZE,	//sampleindex. if at end of sample array sound is not playing
	45,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Perc16cowbell",	// sample name

	Bassharmonicdrivelopulsarg0,	// pointer to sample array
	Bassharmonicdrivelopulsarg0_SIZE,	// size of the sample array
	Bassharmonicdrivelopulsarg0_SIZE,	//sampleindex. if at end of sample array sound is not playing
	46,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bassharmonicdrivelo",	// sample name

	Basssyninsectpulsarg2,	// pointer to sample array
	Basssyninsectpulsarg2_SIZE,	// size of the sample array
	Basssyninsectpulsarg2_SIZE,	//sampleindex. if at end of sample array sound is not playing
	47,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Basssyninsectpulsar",	// sample name

	Ohsimplepulsar01,	// pointer to sample array
	Ohsimplepulsar01_SIZE,	// size of the sample array
	Ohsimplepulsar01_SIZE,	//sampleindex. if at end of sample array sound is not playing
	48,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Ohsimplepulsar01",	// sample name

	Perchobby,	// pointer to sample array
	Perchobby_SIZE,	// size of the sample array
	Perchobby_SIZE,	//sampleindex. if at end of sample array sound is not playing
	49,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Perchobby",	// sample name

	Clap6ixty,	// pointer to sample array
	Clap6ixty_SIZE,	// size of the sample array
	Clap6ixty_SIZE,	//sampleindex. if at end of sample array sound is not playing
	50,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Clap6ixty",	// sample name

	Bdplucklitepulsar,	// pointer to sample array
	Bdplucklitepulsar_SIZE,	// size of the sample array
	Bdplucklitepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	51,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bdplucklitepulsar",	// sample name

	Sddelaycrackpulsar,	// pointer to sample array
	Sddelaycrackpulsar_SIZE,	// size of the sample array
	Sddelaycrackpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	52,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Sddelaycrackpulsar",	// sample name

	Woodblockpulsar,	// pointer to sample array
	Woodblockpulsar_SIZE,	// size of the sample array
	Woodblockpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	53,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Woodblockpulsar",	// sample name

	Synbendpulsar,	// pointer to sample array
	Synbendpulsar_SIZE,	// size of the sample array
	Synbendpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	54,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Synbendpulsar",	// sample name

	Timesstab,	// pointer to sample array
	Timesstab_SIZE,	// size of the sample array
	Timesstab_SIZE,	//sampleindex. if at end of sample array sound is not playing
	55,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Timesstab",	// sample name

	Cymbaldustpulsar,	// pointer to sample array
	Cymbaldustpulsar_SIZE,	// size of the sample array
	Cymbaldustpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	56,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Cymbaldustpulsar",	// sample name

	Chainbass2,	// pointer to sample array
	Chainbass2_SIZE,	// size of the sample array
	Chainbass2_SIZE,	//sampleindex. if at end of sample array sound is not playing
	57,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chainbass2",	// sample name

	Chclassicpulsar06,	// pointer to sample array
	Chclassicpulsar06_SIZE,	// size of the sample array
	Chclassicpulsar06_SIZE,	//sampleindex. if at end of sample array sound is not playing
	58,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chclassicpulsar06",	// sample name

	Chdelaylitepulsar,	// pointer to sample array
	Chdelaylitepulsar_SIZE,	// size of the sample array
	Chdelaylitepulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	59,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Chdelaylitepulsar",	// sample name

	Snapdrypulsar,	// pointer to sample array
	Snapdrypulsar_SIZE,	// size of the sample array
	Snapdrypulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	60,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Snapdrypulsar",	// sample name

	Bdverbdarkpulsar,	// pointer to sample array
	Bdverbdarkpulsar_SIZE,	// size of the sample array
	Bdverbdarkpulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	61,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Bdverbdarkpulsar",	// sample name

	Lofisweeppulsar,	// pointer to sample array
	Lofisweeppulsar_SIZE,	// size of the sample array
	Lofisweeppulsar_SIZE,	//sampleindex. if at end of sample array sound is not playing
	62,	// MIDI note on that plays this sample
	127,	// play volume 0-127
	"Lofisweeppulsar",	// sample name

};
