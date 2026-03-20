#include <ctime>
#include "OSystem.hxx"
#include "SerialPort.hxx"
#include "Paddles.hxx"
#include "PropsSet.hxx"
#include "Console.hxx"
#include "TIA.hxx"
#include "Sound.hxx"
#include "SoundSDL.hxx"


// class NullSound : public Sound
// {
//   public:
//     /**
//       Create a new sound object.  The init method must be invoked before
//       using the object.
//     */
//     NullSound(OSystem* osystem)
//     : Sound(osystem)
//     {
//     }
//
//     /**
//       Destructor
//     */
//     virtual ~NullSound() { };
//
//   public:
//     /**
//       Enables/disables the sound subsystem.
//
//       @param enable  Either true or false, to enable or disable the sound system
//     */
//     virtual void setEnabled(bool enable) { }
//
//     /**
//       The system cycle counter is being adjusting by the specified amount.  Any
//       members using the system cycle counter should be adjusted as needed.
//
//       @param amount The amount the cycle counter is being adjusted by
//     */
//     virtual void adjustCycleCounter(Int32 amount) { }
//
//     /**
//       Sets the number of channels (mono or stereo sound).
//
//       @param channels The number of channels
//     */
//     virtual void setChannels(uInt32 channels) { }
//
//     /**
//       Sets the display framerate.  Sound generation for NTSC and PAL games
//       depends on the framerate, so we need to set it here.
//
//       @param framerate The base framerate depending on NTSC or PAL ROM
//     */
//     virtual void setFrameRate(float framerate) { }
//
//     /**
//       Start the sound system, initializing it if necessary.  This must be
//       called before any calls are made to derived methods.
//     */
//     virtual void open() { }
//
//     /**
//       Should be called to stop the sound system.  Once called the sound
//       device can be started again using the ::open() method.
//     */
//     virtual void close() { }
//
//     /**
//       Set the mute state of the sound object.  While muted no sound is played.
//
//       @param state Mutes sound if true, unmute if false
//     */
//     virtual void mute(bool state) { }
//
//     /**
//       Reset the sound device.
//     */
//     virtual void reset() { }
//
//     /**
//       Sets the sound register to a given value.
//
//       @param addr  The register address
//       @param value The value to save into the register
//       @param cycle The system cycle at which the register is being updated
//     */
//     virtual void set(uInt16 addr, uInt8 value, Int32 cycle) { }
//
//     /**
//       Sets the volume of the sound device to the specified level.  The
//       volume is given as a percentage from 0 to 100.  Values outside
//       this range indicate that the volume shouldn't be changed at all.
//
//       @param percent The new volume percentage level for the sound device
//     */
//     virtual void setVolume(Int32 percent) { }
//
//     /**
//       Adjusts the volume of the sound device based on the given direction.
//
//       @param direction  Increase or decrease the current volume by a predefined
//                         amount based on the direction (1 = increase, -1 =decrease)
//     */
//     virtual void adjustVolume(Int8 direction) { }
//
//     /**
//       Save the current state of the object to the given Serializer.
//
//       @param out  The Serializer object to use
//       @return  False on any errors, else true
//     */
//     virtual bool save(Serializer& out) const { return true; }
//
//     /**
//       Load the current state of the object from the given Serializer.
//
//       @param in  The Serializer object to use
//       @return  False on any errors, else true
//     */
//     virtual bool load(Serializer& in) { return true; }
//
//     /**
//       Get a descriptor for the object name (used in error checking).
//
//       @return The name of the object
//     */
//     virtual string name() const { return string("NullSound"); }
//
// };


OSystem::OSystem()
{
    myNVRamDir = ".";
    mySettings = 0;
    myFrameBuffer = new FrameBuffer();
    //mySound = new NullSound(this);
    mySound = new SoundSDL(this);
    mySerialPort = new SerialPort();
    myEventHandler = new EventHandler(this);
    myPropSet = new PropertiesSet(this);
    Paddles::setDigitalSensitivity(5);
    Paddles::setMouseSensitivity(5);
}

OSystem::~OSystem()
{
    delete myFrameBuffer;
    delete mySound;
    delete mySerialPort;
    delete myEventHandler;
    delete myPropSet;
}

bool OSystem::create() { return 1; }

void OSystem::mainLoop() { }

void OSystem::pollEvent() { }

bool OSystem::queryVideoHardware() { return 1; }

void OSystem::stateChanged(EventHandler::State state) { }

void OSystem::setDefaultJoymap(Event::Type event, EventMode mode) { }

void OSystem::setFramerate(float framerate) { }
//void OSystem::setFramerate(float framerate)
//{
//    if(framerate > 0.0)
//    {
//        myDisplayFrameRate = framerate;
//        myTimePerFrame = (uInt32)(1000000.0 / myDisplayFrameRate);
//    }
//}

void OSystem::setConfigPaths() { }

void OSystem::setUIPalette() { }

void OSystem::deleteConsole() { }

bool OSystem::createLauncher(const string& startdir) { return true; }

string OSystem::getROMInfo(const string& romfile) { return string(); }

string OSystem::MD5FromFile(const string& filename) { return string(); }

void OSystem::setBaseDir(const string& basedir) { }

void OSystem::setConfigFile(const string& file) { }

FBInitStatus OSystem::createFrameBuffer() { return (FBInitStatus) 0; }

void OSystem::createSound() { }

Console* OSystem::openConsole(const string& romfile, string& md5, string& type, string& id) { return 0; }

uInt8* OSystem::openROM(string rom, string& md5, uInt32& size) { return 0; }

string OSystem::getROMInfo(const Console* console) { return string(); }

void OSystem::resetLoopTiming() { }

void OSystem::validatePath(string& path, const string& setting,
const string& defaultpath) { }

OSystem& OSystem::operator = (const OSystem&) { return *this; }


uInt64 OSystem::getTicks() const
{
    return myConsole->tia().getMilliSeconds();
}

EventHandler::EventHandler(OSystem*)
{

}

EventHandler::~EventHandler()
{

}

FrameBuffer::FrameBuffer()
{

}

FrameBuffer::~FrameBuffer()
{

}

FBInitStatus FrameBuffer::initialize(const string& title, uInt32 width, uInt32 height)
{
    //logMsg("called FrameBuffer::initialize, %d,%d", width, height);
    return kSuccess;
}

void FrameBuffer::refresh() { }

void FrameBuffer::showFrameStats(bool enable) { }

// 0 to <counts> - 1, i_s caches the value of counts
//#define iterateTimes(counts, i) for(unsigned int i = 0, i ## _s = counts; i < (i ## _s); i++)
//void FrameBuffer::setTIAPalette(const uInt32* palette)
//{
//}
