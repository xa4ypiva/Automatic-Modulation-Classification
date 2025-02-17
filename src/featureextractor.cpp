#include "featureextractor.h"

AMC::FeatureExtractor::FeatureExtractor(boost::shared_ptr<SharedBuffer<std::complex<double> > > buffer,
        double fs,
        boost::shared_ptr<SharedType<double> > fcRelative,
        boost::shared_ptr<SharedType<double> > bwRelative,
        size_t windowSize,
        size_t modTypeWindowSize) :
    _buffer(buffer),  
	_x(windowSize), 
	_windowSize(windowSize), 
	_fs(fs), 
	_fileWriter(),
    _classifier(),
    _sharedModType(new SharedType<AMC::ModType>()),
    _sharedFcRelative(fcRelative),
    _sharedBwRelative(bwRelative),
    _maxModTypes(modTypeWindowSize),
    _modTypes(),
    _modTypeTally()
{}

boost::shared_ptr< SharedType<AMC::ModType > > AMC::FeatureExtractor::getSharedModType()
{
    return _sharedModType;
}

void AMC::FeatureExtractor::setClassifier(AmcClassifier<double, AMC::ModType> *classifier)
{
    _classifier.reset(classifier);
}

void AMC::FeatureExtractor::setBuffer(boost::shared_ptr<SharedBuffer<std::complex<double> > > buff)
{
    _buffer.swap(buff);
}

AMC::ModType AMC::FeatureExtractor::getSlowModType()
{
    AMC::ModType maxCount;
    size_t maxVal = 0;
    // Reset all enum values to 0.
    _modTypeTally[AMC::ModType::AM_DSB_FC] = 0;
    _modTypeTally[AMC::ModType::AM_DSB_SC] = 0;
    _modTypeTally[AMC::ModType::AM_USB_FC] = 0;
    _modTypeTally[AMC::ModType::AM_USB_SC] = 0;
    _modTypeTally[AMC::ModType::AM_LSB_FC] = 0;
    _modTypeTally[AMC::ModType::AM_LSB_SC] = 0;
    _modTypeTally[AMC::ModType::FM] = 0;
    _modTypeTally[AMC::ModType::MPSK] = 0;
    _modTypeTally[AMC::ModType::PSK_2] = 0;
    _modTypeTally[AMC::ModType::MASK] = 0;
    _modTypeTally[AMC::ModType::ASK_2] = 0;
    _modTypeTally[AMC::ModType::MQAM] = 0;

    for(AMC::ModType modType : _modTypes)
    {
        _modTypeTally[modType] += 1;
    }

    for(std::map<AMC::ModType, size_t>::iterator _map_itr = _modTypeTally.begin();
        _map_itr != _modTypeTally.end(); ++_map_itr)
    {
        if(_map_itr->second > maxVal)
        {
            maxVal = _map_itr->second;
            maxCount = _map_itr->first;
        }
    }
    return maxCount;
}

void AMC::FeatureExtractor::start(ExtractionMode mode)
{
    _isExtracting = true;
    _mode = mode;

    if(mode == CLASSIFY)
    {
        if(!_classifier)
            throw std::runtime_error("FeatureExtractor Error: No classifier specified.");
    }

    _extractorThread = boost::thread(&FeatureExtractor::run, this);
}

void AMC::FeatureExtractor::start(ExtractionMode mode, AMC::ModType modType)
{
    if (mode == WRITE_TO_FILE)
    {
        _mode = mode;
        _fileWriter.newFile(modType);
        _fileWriter.start();
        _isExtracting = true;
        _extractorThread = boost::thread(&FeatureExtractor::run, this);
    }
}

void AMC::FeatureExtractor::stop()
{
    _mode = AMC::FeatureExtractor::ExtractionMode::STOPPED;
    _isExtracting = false;
    _fileWriter.stop();
    _extractorThread.join();
}

void AMC::FeatureExtractor::run()
{
    while (_isExtracting)
    {
        if(get_x())
        {
            boost::shared_lock<boost::shared_mutex> fcLock(*_sharedFcRelative->getMutex());
            _fnc = round(_sharedFcRelative->getData() * _windowSize);
            fcLock.unlock();

            _featureThread0 = boost::thread(&AMC::FeatureExtractor::findSigmaAMu42A, this);
            AMC::FeatureExtractor::findMu42FSigmaAF();

            _featureThread0.join();
            switch(_mode)
            {
            case AMC::FeatureExtractor::WRITE_TO_FILE:
                _fileWriter.writeToFile(AMC::FeatureExtractor::getFeatureVector());
                break;
            case AMC::FeatureExtractor::STOPPED:
                break;
            case AMC::FeatureExtractor::CLASSIFY:
                _modTypes.push_back(_classifier->classify(AMC::FeatureExtractor::getFeatureVector()));
                if(_modTypes.size() > _maxModTypes)
                    _modTypes.pop_front();

                boost::unique_lock<boost::shared_mutex> modTypeLock(*_sharedModType->getMutex());
                _sharedModType->getData() = getSlowModType();
                modTypeLock.unlock();
                break;
            }
        }
        else
        {
            //TODO: How long to sleep?
            boost::this_thread::sleep(boost::posix_time::milliseconds(10));
        }
    }
}

bool AMC::FeatureExtractor::get_x()
{
    boost::shared_lock<boost::shared_mutex> bufferLock(*_buffer->getMutex());
    boost::unique_lock<boost::shared_mutex> xWriteLock(*_x.getMutex());

    if(_buffer->getBuffer().size() >= _windowSize)
    {
        for (size_t n = 0; n < _windowSize; ++n)
        {
            _x.getData()[n] = _buffer->getBuffer()[n];
        }

        _x.getData() = AMC::normalize(AMC::center(_x.getData()));

        bufferLock.unlock();
        xWriteLock.unlock();
        return true;
    }

    bufferLock.unlock();
    xWriteLock.unlock();

    return false;
}

void AMC::FeatureExtractor::findSigmaAMu42A()
{
    boost::shared_lock<boost::shared_mutex> xLock(*_x.getMutex());
    boost::unique_lock<boost::shared_mutex> xNormCenterWriteLock(*_xNormCenter.getMutex());

    _xNormCenter.getData() = AMC::normalize(AMC::center(_x.getData()));

    xLock.unlock();
    xNormCenterWriteLock.unlock();

    boost::shared_lock<boost::shared_mutex> xNormCenterReadLock(*_xNormCenter.getMutex());
    _featureThread1 = boost::thread(&AMC::FeatureExtractor::findSigmaAA, this);

    AMC::stdDevKurtosis( _xNormCenter.getData(), _sigmaA, _mu42A);
    xNormCenterReadLock.unlock();

	_featureThread1.join();
}

void AMC::FeatureExtractor::findMu42FSigmaAF()
{
    boost::shared_lock<boost::shared_mutex> xLock(*_x.getMutex());
    boost::unique_lock<boost::shared_mutex> xFftWriteLock(*_xFft.getMutex());

    _xFft.getData() = AMC::fft(_x.getData());
    xLock.unlock();
    xFftWriteLock.unlock();

    boost::shared_lock<boost::shared_mutex> xFftReadLock(*_xFft.getMutex());

    _featureThread2 = boost::thread(&AMC::FeatureExtractor::findGammaMaxP, this);

    auto xAnalFreq = AMC::removeNegFreq(_xFft.getData());
    xFftReadLock.unlock();

    auto xAnal = AMC::ifft(xAnalFreq);

    auto xWrappedPhase = AMC::phase(xAnal);

    boost::unique_lock<boost::shared_mutex> xPhaseWriteLock(*_xPhase.getMutex());
    _xPhase.getData() = AMC::unwrapPhase(xWrappedPhase);
    xPhaseWriteLock.unlock();

    _featureThread3 = boost::thread(&AMC::FeatureExtractor::findSigmaDP, this);

    boost::shared_lock<boost::shared_mutex> xPhaseLock(*_xPhase.getMutex());
    std::vector<double> xRelativeInstFreq = AMC::differentiate(_xPhase.getData()); //linear shift when finding actual freq
    xPhaseLock.unlock();

    auto xRelativeInstFreqN = AMC::normalize(xRelativeInstFreq);
    std::vector<double> xRelativeInstFreqNormCenter = AMC::center(xRelativeInstFreqN);
    AMC::stdDevKurtosis(xRelativeInstFreqNormCenter, _mu42F, _sigmaAF);

	_featureThread2.join();
	_featureThread3.join();
}

void AMC::FeatureExtractor::findSigmaAA()
{
    boost::shared_lock<boost::shared_mutex> xNormCenter(*_xNormCenter.getMutex());
    auto xNormCenterAbs = AMC::abs(_xNormCenter.getData());
    xNormCenter.unlock();

    _sigmaAA = AMC::stdDev(xNormCenterAbs);
}

void AMC::FeatureExtractor::findGammaMaxP()
{
    boost::shared_lock<boost::shared_mutex> xFftLock(*_xFft.getMutex());

	size_t unused;
    _gammaMax = AMC::maxPower(_xFft.getData(), unused);

    double bw = _sharedBwRelative->getData();
    _P = AMC::symmetry(_xFft.getData(), _fnc, bw);
    xFftLock.unlock();
}

void AMC::FeatureExtractor::findSigmaDP()
{
    boost::shared_lock<boost::shared_mutex> xPhaseLock(*_xPhase.getMutex());
    boost::unique_lock<boost::shared_mutex> xPhaseNLWriteLock(*_xPhaseNL.getMutex());

    _xPhaseNL.getData() = AMC::removeLinearPhase(_xPhase.getData(), _fnc);
    xPhaseLock.unlock();
    xPhaseNLWriteLock.unlock();

    _featureThread4 = boost::thread(&AMC::FeatureExtractor::findSigmaAP, this);

    boost::shared_lock<boost::shared_mutex> xPhaseNLReadLock(*_xPhaseNL.getMutex());
    _sigmaDP = AMC::stdDev(_xPhaseNL.getData());
    xPhaseNLReadLock.unlock();

	_featureThread4.join();
}

void AMC::FeatureExtractor::findSigmaAP()
{
    boost::shared_lock<boost::shared_mutex> xPhaseNLLock(*_xPhaseNL.getMutex());
    std::vector<double> xAbsPhase = AMC::abs(_xPhaseNL.getData());
    xPhaseNLLock.unlock();

    _sigmaAP = AMC::stdDev(xAbsPhase);
}

std::vector<double> AMC::FeatureExtractor::getFeatureVector()
{
    std::vector<double> f = {_mu42F, _sigmaAF, _sigmaDP, _sigmaAP, _gammaMax, _P, _sigmaA, _mu42A, _sigmaAA};
    return f;
}


