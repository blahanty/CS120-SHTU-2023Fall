#pragma once

#include <JuceHeader.h>
#include <fstream>
#include <vector>
#include <deque>

using namespace std;
using namespace juce;

#define BIT_WIDTH 60
#define PREAMBLE_LENGTH 480
#define BIT_NUM 10000
#define SUM_THRESHOLD 10
#define FREQ 600

class MainComponent : public juce::AudioAppComponent {
public:
	MainComponent();

	~MainComponent() override;

	void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;

	void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

	void releaseResources() override;

private:
	enum TransportState {
		Ready, ToSend, Sending, ToReceive, Receiving
	};

	void changeState(TransportState newState) {
		if (state != newState) {
			state = newState;

			switch (state) {
			case Ready:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(false);
				break;

			case ToSend:
				openButton.setEnabled(true);
				sendButton.setEnabled(true);
				receiveButton.setEnabled(false);
				break;

			case ToReceive:
				openButton.setEnabled(true);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(true);
				break;

			case Sending:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(false);
				break;

			case Receiving:
				openButton.setEnabled(false);
				sendButton.setEnabled(false);
				receiveButton.setEnabled(true);
				break;
			}
		}
	}

	void openButtonClicked() {
		sentData.clear();
		auto chooser = new juce::FileChooser("Select input file or output directory ...", File::getSpecialLocation(File::SpecialLocationType::userDesktopDirectory));
		auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories;

		chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
			auto file = fc.getResult();

			if (file != juce::File{}) {
				auto path = fc.getResult().getFullPathName();
				if (path.contains(".txt")) {
					FileInputStream iStream(file);
					String iString = iStream.readString();

					sentData.clear();
					sentData.insert(sentData.end(), preambleWave.begin(), preambleWave.end());

					for (char c : iString) {
						auto inputChar = static_cast<char>(c);
						if (inputChar == '1')
							sentData.insert(sentData.end(), carrierWave.begin(), carrierWave.end());
						else if (inputChar == '0')
							sentData.insert(sentData.end(), zeroWave.begin(), zeroWave.end());
					}
					changeState(ToSend);
				}
				else if (!path.contains(".")) {
					File output(path + "/OUTPUT.txt");
					outputFile = output;
					changeState(ToReceive);
				}
			}});
	}

	void sendButtonClicked() {
		changeState(Sending);
	}

	void receiveButtonClicked() {
		switch (state)
		{
		case ToReceive:
		{
			receivedData.clear();
			receiveButton.setButtonText("Stop");
			receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
			changeState(Receiving);
			break;
		}

		case Receiving:
			receiveButton.setButtonText("Receive");
			receiveButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue);
			changeState(Ready);
			break;
		}
	}

	void setSampleBuffer(juce::AudioSampleBuffer* sampleBuffer) { this->sampleBuffer = sampleBuffer; }

	void setSampleRate(double sampleRate) { this->sampleRate = sampleRate; }

	void setReadPointer(int readPointer) { this->readPointer = readPointer; }

	void setWritePointer(int writePointer) { this->writePointer = writePointer; }

	TransportState state;
	TextButton openButton, sendButton, receiveButton;
	AudioSampleBuffer* sampleBuffer = nullptr;
	double sampleRate;
	int readPointer = 0, writePointer = 0;

	vector<float> carrierWave, zeroWave;
	vector<float> sentData;
	deque<float> receivedData;
	float sum = 0, maxSum = SUM_THRESHOLD;
	int lowerTicks = 0, bitsReceived = 0;
	File outputFile;
	bool begin = false, preambleSuspected = false;


	const vector<float> preambleWave = { 1, 0.4981031, -0.5037913, -0.9999783, -0.4923696, 0.5094954, 0.9999127, 0.4865715,
							  -0.5152195, -0.999802, -0.4807087, 0.5209628, 0.9996455, 0.474781, -0.5267245,
							  -0.999442, -0.4687881, 0.5325038, 0.9991905, 0.4627298, -0.5382999, -0.9988901,
							  -0.456606, 0.544112, 0.9985398, 0.4504165, -0.5499393, -0.9981386, -0.4441611,
							  0.555781, 0.9976854, 0.4378397, -0.5616361, -0.9971793, -0.4314521, 0.5675038,
							  0.9966191, 0.4249981, -0.5733832, -0.996004, -0.4184777, 0.5792734, 0.9953329,
							  0.4118908, -0.5851735, -0.9946046, -0.4052371, 0.5910825, 0.9938183, 0.3985168,
							  -0.5969995, -0.9929729, -0.3917296, 0.6029236, 0.9920673, 0.3848756, -0.6088537,
							  -0.9911006, -0.3779546, 0.6147888, 0.9900715, 0.3709666, -0.620728, -0.9889792,
							  -0.3639117, 0.6266702, 0.9878226, 0.3567898, -0.6326143, -0.9866006, -0.3496009,
							  0.6385593, 0.9853122, 0.3423451, -0.6445042, -0.9839563, -0.3350223, 0.6504478,
							  0.9825318, 0.3276327, -0.656389, -0.9810379, -0.3201763, 0.6623267, 0.9794732,
							  0.3126531, -0.6682598, -0.977837, -0.3050634, 0.6741871, 0.976128, 0.2974072,
							  -0.6801074, -0.9743452, -0.2896846, 0.6860195, 0.9724877, 0.2818958, -0.6919223,
							  -0.9705542, -0.274041, 0.6978145, 0.9685439, 0.2661204, -0.7036949, -0.9664557,
							  -0.2581341, 0.7095622, 0.9642884, 0.2500824, -0.7154152, -0.9620412, -0.2419656,
							  0.7212525, 0.9597128, 0.2337839, -0.7270729, -0.9573025, -0.2255375, 0.732875,
							  0.954809, 0.2172269, -0.7386575, -0.9522313, -0.2088522, 0.7444191, 0.9495686,
							  0.200414, -0.7501584, -0.9468196, -0.1919124, 0.755874, 0.9439835, 0.183348,
							  -0.7615646, -0.9410593, -0.1747211, 0.7672286, 0.9380458, 0.1660322, -0.7728647,
							  -0.9349422, -0.1572817, 0.7784714, 0.9317475, 0.14847, -0.7840473, -0.9284607,
							  -0.1395978, 0.7895908, 0.9250808, 0.1306655, -0.7951006, -0.9216068, -0.1216736,
							  0.800575, 0.9180379, 0.1126227, -0.8060126, -0.914373, -0.1035135, 0.8114118,
							  0.9106113, 0.0943465, -0.8167711, -0.9067517, -0.0851223, 0.8220889, 0.9027935,
							  0.0758417, -0.8273636, -0.8987357, -0.0665054, 0.8325937, 0.8945774, 0.057114,
							  -0.8377774, -0.8903177, -0.0476684, 0.8429133, 0.8859558, 0.0381692, -0.8479996,
							  -0.8814907, -0.0286173, 0.8530347, 0.8769218, 0.0190135, -0.8580169, -0.8722481,
							  -0.0093586, 0.8629446, 0.8674688, -0.0003464, -0.867816, -0.8625832, 0.0101007,
							  0.8726295, 0.8575904, -0.0199033, -0.8773834, -0.8524897, 0.0297533, 0.8820758,
							  0.8472803, -0.0396496, -0.8867052, -0.8419616, 0.0495912, 0.8912696, 0.8365327,
							  -0.0595772, -0.8957674, -0.830993, 0.0696064, 0.9001968, 0.8253418, -0.0796777,
							  -0.904556, -0.8195785, 0.0897899, 0.9088432, 0.8137024, -0.0999419, -0.9130566,
							  -0.8077128, 0.1101326, 0.9171943, 0.8016093, -0.1203606, -0.9212546, -0.7953911,
							  0.1306247, 0.9252356, 0.7890578, -0.1409237, -0.9291355, -0.7826088, 0.1512561,
							  0.9329525, 0.7760437, -0.1616207, -0.9366846, -0.7693618, 0.172016, 0.94033,
							  0.7625628, -0.1824407, -0.9438868, -0.7556463, 0.1928933, 0.9473532, 0.7486117,
							  -0.2033723, -0.9507272, -0.7414588, 0.2138762, 0.954007, 0.7341871, -0.2244035,
							  -0.9571908, -0.7267964, 0.2349526, 0.9602765, 0.7192863, -0.2455218, -0.9632622,
							  -0.7116566, 0.2561096, 0.9661462, 0.703907, -0.2667143, -0.9689264, -0.6960372,
							  0.2773341, 0.971601, 0.6880472, -0.2879673, -0.974168, -0.6799367, 0.2986122,
							  0.9766256, 0.6717057, -0.309267, -0.9789718, -0.663354, 0.3199298, 0.9812047,
							  0.6548816, -0.3305987, -0.9833224, -0.6462885, 0.3412719, 0.9853229, 0.6375746,
							  -0.3519474, -0.9872044, -0.6287401, 0.3626233, 0.988965, 0.619785, -0.3732975,
							  -0.9906028, -0.6107094, 0.383968, 0.9921158, 0.6015135, -0.3946328, -0.9935022,
							  -0.5921974, 0.4052897, 0.99476, 0.5827615, -0.4159367, -0.9958875, -0.5732059,
							  0.4265715, 0.9968827, 0.5635311, -0.4371921, -0.9977437, -0.5537372, 0.447796,
							  0.9984687, 0.5438248, -0.4583812, -0.9990559, -0.5337942, 0.4689453, 0.9995034,
							  0.523646, -0.479486, -0.9998093, -0.5133806, 0.4900009, 0.999972, 0.5029986,
							  -0.5004877, -0.9999894, -0.4925007, 0.5109439, 0.99986, 0.4818873, -0.5213671,
							  -0.9995818, -0.4711594, 0.5317549, 0.9991532, 0.4603175, -0.5421047, -0.9985724,
							  -0.4493624, 0.5524139, 0.9978376, 0.4382951, -0.5626802, -0.9969472, -0.4271164,
							  0.5729007, 0.9958994, 0.4158271, -0.583073, -0.9946926, -0.4044283, 0.5931944,
							  0.9933252, 0.3929211, -0.6032622, -0.9917955, -0.3813064, 0.6132736, 0.9901019,
							  0.3695853, -0.6232261, -0.9882429, -0.3577592, 0.6331168, 0.9862168, 0.3458291,
							  -0.6429429, -0.9840222, -0.3337964, 0.6527017, 0.9816575, 0.3216624, -0.6623903,
							  -0.9791212, -0.3094285, 0.6720059, 0.976412, 0.297096, -0.6815455, -0.9735285,
							  -0.2846666, 0.6910064, 0.9704691, 0.2721416, -0.7003855, -0.9672326, -0.2595228,
							  0.7096799, 0.9638176, 0.2468118, -0.7188867, -0.9602229, -0.2340102, 0.7280029,
							  0.9564473, 0.2211198, -0.7370254, -0.9524894, -0.2081425, 0.7459513, 0.9483481,
							  0.1950801, -0.7547775, -0.9440224, -0.1819345, 0.763501, 0.939511, 0.1687077,
							  -0.7721186, -0.9348129, -0.1554018, 0.7806274, 0.9299272, 0.1420189, -0.7890242,
							  -0.9248528, -0.128561, 0.7973058, 0.9195889, 0.1150305, -0.8054693, -0.9141345,
							  -0.1014296, 0.8135113, 0.9084887, 0.0877605, -0.8214288, -0.902651, -0.0740258,
							  0.8292187, 0.8966203, 0.0602278, -0.8368776, -0.8903962, -0.046369, 0.8444026,
							  0.883978, 0.0324521, -0.8517904, -0.877365, -0.0184795, 0.8590377, 0.8705568,
							  0.0044541, -0.8661415, -0.8635528, 0.0096215, 0.8730986, 0.8563527, -0.0237445,
							  -0.8799056, -0.8489561, 0.037912, 0.8865595, 0.8413627, -0.0521211, -0.8930571,
							  -0.8335722, 0.0663688, 0.8993951, 0.8255845, -0.0806521, -0.9055704, -0.8173995,
							  0.094968, 0.9115798, 0.809017 };

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
