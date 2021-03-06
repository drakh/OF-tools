#include "OpenNITracker.h"

OpenNI::OpenNI()
{
    kinectWidth = 640;
    kinectHeight = 480;
    
    grayImage.allocate(kinectWidth, kinectHeight);
	grayThreshNear.allocate(kinectWidth, kinectHeight);
	grayThreshFar.allocate(kinectWidth, kinectHeight);
    rgbImage.allocate(kinectWidth, kinectHeight);
    
    calibrating = false;
    trackingContours = false;
    trackingUsers = false;
    depthMaskEnabled = false;
    
    // contour parameters
    minArea = 5000;
    maxArea = 140000;
    threshold = 15;
    persistence = 15;
    maxDistance = 32;
    nearThreshold = 11;
    farThreshold = 0;
    smoothingRate = 1;
    numFrames = 1024;

    // user tracking
    numTrackedUsers = 0;
    maxUsers = 1;
    
    depthHistory.resize(numFrames);
    
    panel.disableControlRow();
    panel.setName("OpenNI");
    toggleCalibrate = panel.addToggle("calibrate", &calibrating, this, &OpenNI::eventToggleCalibrationModule);
    panel.addToggle("track contours", &trackingContours, this, &OpenNI::eventSetTrackingContours);
    panel.addToggle("track users", &trackingUsers, this, &OpenNI::eventSetTrackingUsers);
    
    panelCalibration = panel.addWidget("calibration");
    panelCalibration->add2dPad("chessboard", &calibration.getChessboard().position, ofPoint(0, 0), ofPoint(ofGetWidth(), ofGetHeight()));
    panelCalibration->addSlider("size", &calibration.getChessboard().size, 3, ofGetWidth());
    panelCalibration->addButton("add point", this, &OpenNI::eventAddPointPairs);
    panelCalibration->addButton("calibrate", this, &OpenNI::eventCalibrate);
    panelCalibration->addButton("save", this, &OpenNI::eventSaveCalibration);
    panelCalibration->addButton("load", this, &OpenNI::eventLoadCalibration);

    panelTracking = panel.addWidget("contour tracking");
    panelTracking->addToggle("setup mask", &depthMaskEnabled, this, &OpenNI::eventSetupMask);
    panelTracking->addSlider("farThreshold", &farThreshold, 0, 255);
    panelTracking->addSlider("nearThreshold", &nearThreshold, 0, 255);
    panelTracking->addSlider("minArea", &minArea, 0, 100000);
    panelTracking->addSlider("maxArea", &maxArea, 2500, 150000);
    panelTracking->addSlider("threshold", &threshold, 0, 255);
    panelTracking->addSlider("persistence", &persistence, 0, 100);
    panelTracking->addSlider("maxDistance", &maxDistance, 0, 100);
    panelTracking->addSlider("smoothingRate", &smoothingRate, 0, 100);
    panelTracking->addToggle("simplified", &simplified);
    panelTracking->addSlider("delay", &delay, 0, numFrames);
    
    panelUsers = panel.addWidget("track users");
    panelUsers->addSlider("max users", &maxUsers, 1, 10, this, &OpenNI::eventSetMaxUsers);
    panelUsers->setCollapsible(true);

    toggleCalibrate->setActive(false);
    panelCalibration->setActive(false);
    panelTracking->setActive(false);
    panelUsers->setActive(false);
}

OpenNI::~OpenNI()
{
    ofRemoveListener(kinect.userEvent, this, &OpenNI::eventUser);
    clearUsers();
    stop();
}

void OpenNI::setup(string oni)
{
    oni != "" ? kinect.setupFromONI(oni) : kinect.setup();
    kinect.addDepthGenerator();
    kinect.setRegister(true);
    kinect.setMirror(false);
    kinect.setUseDepthRawPixels(true);
    kinect.setDepthColoring(COLORING_GREY);
    kinect.start();
    
    setTrackingUsers(false);
    setTrackingUserFeatures(false);
    setTrackingContours(false);
    
    ofAddListener(kinect.userEvent, this, &OpenNI::eventUser);
}

void OpenNI::stop()
{
    kinect.stop();
}

void OpenNI::setTrackingUsers(bool trackingUsers)
{
    this->trackingUsers = trackingUsers;
    if (trackingUsers)
    {
        panelUsers->setActive(true);
        kinect.addUserGenerator();
        kinect.setMaxNumUsers(maxUsers);
        kinect.setUseMaskPixelsAllUsers(false);
        kinect.setUseMaskTextureAllUsers(false);
        kinect.setUsePointCloudsAllUsers(false);
    }
    else
    {
        setTrackingUserFeatures(false);
        panelUsers->setActive(false);
        kinect.removeUserGenerator();
    }
}

void OpenNI::setTrackingUserFeatures(bool trackingUserFeatures)
{
    this->trackingUserFeatures = trackingUserFeatures;
    if (trackingUserFeatures)
    {
        map<int, OpenNIUser*>::iterator it = users.begin();
        for (; it != users.end(); ++it) {
            it->second->setFeatureTrackingEnabled(true);
        }
    }
    else {
        clearUsers();
    }
}

void OpenNI::setTrackingContours(bool trackingContours)
{
    this->trackingContours = trackingContours;
    panelTracking->setActive(trackingContours);
}

ofVec3f OpenNI::getWorldCoordinateAt(int x, int y)
{
    int idx = (idxHistory - 1 - delay + numFrames) % numFrames;
    ofPoint depthPoint = ofPoint(x, y, depthHistory[idx][x + y * kinectWidth]);
    ofVec3f worldPoint = kinect.projectiveToWorld(depthPoint);
    return worldPoint;
}

ofVec2f OpenNI::getProjectedPointAt(int x, int y)
{
    return kpt.getProjectedPoint(getWorldCoordinateAt(x, y));
}

void OpenNI::getCalibratedContour(int idx, vector<ofVec2f> & calibratedPoints, int width, int height, float smoothness)
{
    smoothness = max(smoothness, 1.0f);
    ofVec2f projectedPoint, mappedPoint;
    //vector<cv::Point> & points = contourFinder.getContour(idx);
    ofPolyline &line = contourFinder.getPolyline(idx);
    line.simplify(smoothness);
    vector<ofPoint> & vertices = line.getVertices();
    for (float j = 0; j < vertices.size(); j++)
    {
        projectedPoint.set(kpt.getProjectedPoint(getWorldCoordinateAt(vertices[j].x, vertices[j].y)));
        mappedPoint.set(width * projectedPoint.x, height - height * projectedPoint.y);
        calibratedPoints.push_back(mappedPoint);
    }
}

bool OpenNI::update()
{
    panel.update();
    
    kinect.update();
    if (kinect.isNewFrame())
    {
        depthHistory[idxHistory] = kinect.getDepthRawPixels();
        idxHistory = (idxHistory + 1) % numFrames;
        
        if (calibrating) {
            updateCalibration();
        }
        if (trackingUsers) {
            updateUsers();
        }
        if (trackingContours) {
            updateContours();
        }
        
        return true;
    }
    else
    {
        return false;
    }
}

void OpenNI::updateUsers()
{
    numTrackedUsers = kinect.getNumTrackedUsers();
    
    for (int i = 0; i < numTrackedUsers; i++)
    {
        ofxOpenNIUser * user = &kinect.getTrackedUser(i);
        if (user->isSkeleton() && users.count(user->getXnID()) == 0)
        {
            OpenNIUser *newUser = new OpenNIUser(user);
            newUser->setFeatureTrackingEnabled(trackingUserFeatures);
            users[user->getXnID()] = newUser;
        }
        else if (trackingUserFeatures)
        {
            if (isNewSkeletonDataAvailable(*user)) {
                users[user->getXnID()]->update();
            }
        }
    }
    
    resetUserGenerator();
}

void OpenNI::clearUsers()
{
    map<int, OpenNIUser*>::iterator it = users.begin();
    for (; it != users.end(); ++it) {
        delete it->second;
    }
    users.clear();
}

void OpenNI::updateSkeletonFeatures()
{
}

void OpenNI::updateContours()
{
    int idx = (idxHistory - 1 - delay + numFrames) % numFrames;
    grayImage.setFromPixels(depthHistory[idx]);
    
    if (depthMaskEnabled)
    {
        cvMask.setFromPixels(maskImage.getPixels(), kinectWidth, kinectHeight);
        cvInRangeS(cvMask.getCvImage(), cvScalarAll(250), cvScalarAll(255), cvGrayMask.getCvImage());
        cvSet(grayImage.getCvImage(), cvScalarAll(0), cvGrayMask.getCvImage());
    }
    
    grayThreshNear = grayImage;
    grayThreshFar = grayImage;
    grayThreshNear.threshold(nearThreshold, true);
    grayThreshFar.threshold(farThreshold);
    cvAnd(grayThreshNear.getCvImage(), grayThreshFar.getCvImage(), grayImage.getCvImage(), NULL);
    grayImage.flagImageChanged();
    contourFinder.setMinArea(minArea);
    contourFinder.setMaxArea(maxArea);
    contourFinder.setThreshold(threshold);
    contourFinder.setSimplify(simplified);
    contourFinder.getTracker().setPersistence(persistence);
    contourFinder.getTracker().setMaximumDistance(maxDistance);
    contourFinder.getTracker().setSmoothingRate(smoothingRate);
    contourFinder.findContours(grayImage);
    numContours = contourFinder.size();
}

void OpenNI::resetUserGenerator()
{
    if (kinect.getNumTrackedUsers()) {
        hadUsers = true;
    }
    else if (!kinect.getNumTrackedUsers() && hadUsers)
    {
        hadUsers = false;
        kinect.setPaused(true);
        kinect.removeUserGenerator();
        kinect.addUserGenerator();
        kinect.setPaused(false);
    }
}

void OpenNI::draw()
{
    if (calibrating) {
        drawCalibration();
    }
    else {
        drawDebug();
    }
    
    panel.draw();
}

void OpenNI::drawDebug()
{
    ofPushStyle();

    ofPushMatrix();
    ofTranslate(200, 0);
    
    if (trackingContours)
    {
        grayImage.draw(0, 0);
        ofSetColor(255, 0, 0);
        ofSetLineWidth(4);
        contourFinder.draw();
        ofSetColor(255);
    }
    else {
        kinect.drawDepth();
    }

    if (trackingUsers) {
        kinect.drawSkeletons();
    }
    
    ofPopMatrix();

    if (depthMaskEnabled) {
        maskPad->draw();
    }

    ofPopStyle();
}

void OpenNI::applyDepthMask()
{
    ofPushStyle();
    
    maskFBO.begin();
    ofClear(0, 0, 0);
    ofDisableAlphaBlending();
    ofFill();
    ofSetColor(255);
    ofRect(0, 0, 640, 480);
    ofSetColor(0, 0, 0);
    ofBeginShape();
    for (int i = 0; i < maskPad->getNumberOfPoints(); i++)
    {
        ofPoint p = maskPad->getParameterValue(i);
        ofVertex(p.x, p.y);
    }
    ofEndShape();
    ofPopStyle();
    maskFBO.end();
    maskFBO.readToPixels(maskPixels);
    maskImage.setFromPixels(maskPixels);
}

void OpenNI::eventSetupMask(GuiButtonEventArgs & e)
{
    if (depthMaskEnabled)
    {
        cvMask.allocate(kinectWidth, kinectHeight);
        cvGrayMask.allocate(kinectWidth, kinectHeight);
        
        maskPixels.allocate(kinectWidth, kinectHeight, OF_PIXELS_RGB);    // the channel count for all three have to be indentical!
        maskImage.allocate(kinectWidth, kinectHeight, OF_IMAGE_COLOR);
        maskFBO.allocate(kinectWidth, kinectHeight, GL_RGB);    // fbo for drawing a new mask
        
        maskPad = new Gui2dPad("mask", ofPoint(0, 0), ofPoint(kinectWidth, kinectHeight));
        maskPad->setCollapsible(false);
        maskPad->setDrawConnectedPoints(true);
        maskPad->setRectangle(ofRectangle(200, 0, kinectWidth, kinectHeight));
        maskPad->setColorBackground(ofColor(0, 0, 0, 0));
        maskPad->setColorForeground(ofColor(0, 255, 0));
        maskPad->setAutoDraw(false);
        ofAddListener(maskPad->padEvent, this, &OpenNI::eventDepthMaskEdited);
    }
    else
    {
        cvMask.allocate(0, 0);
        cvGrayMask.allocate(0, 0);
        maskPixels.allocate(0, 0, OF_PIXELS_RGB);
        maskImage.allocate(0, 0, OF_IMAGE_COLOR);
        maskFBO.allocate(0, 0, GL_RGB);
        delete maskPad;
    }
}

inline bool OpenNI::isNewSkeletonDataAvailable(ofxOpenNIUser & user)
{
    return (user.getJoint(JOINT_TORSO).getWorldPosition() != ofPoint(0,0,0) && (users.count(user.getXnID()) || user.getJoint(JOINT_TORSO).getWorldPosition() != users[user.getXnID()]->getPosition(0) ));
}

void OpenNI::eventSetTrackingUsers(GuiButtonEventArgs & b)
{
    setTrackingUsers(trackingUsers);
}

void OpenNI::eventSetMaxUsers(GuiSliderEventArgs<int> & e)
{
    kinect.setMaxNumUsers(maxUsers);
}

void OpenNI::eventSetTrackingContours(GuiButtonEventArgs & b)
{
    setTrackingContours(trackingContours);
}

void OpenNI::eventSetTrackingUserFeatures(GuiButtonEventArgs & b)
{
    setTrackingUserFeatures(trackingUserFeatures);
}

void OpenNI::eventToggleCalibrationModule(GuiButtonEventArgs & b)
{
    if (calibrating) {
        startCalibrationModule();
    }
    else {
        stopCaibrationModule();
    }
}

void OpenNI::eventDepthMaskEdited(Gui2dPadEventArgs &e)
{
    applyDepthMask();
}

void OpenNI::eventUser(ofxOpenNIUserEvent & event)
{
    ofLog(OF_LOG_NOTICE, "User event: " + ofToString(event.userStatus) + " " + ofToString(event.id));
    
    if (event.userStatus == USER_TRACKING_STARTED) {
        ofLog(OF_LOG_NOTICE, "OpenNI: TRACK STARTED FOR  :: "+ofToString(event.id));
    }
    else if (event.userStatus == USER_TRACKING_STOPPED)
    {
        ofLog(OF_LOG_NOTICE, "OpenNI: TRACK STOPPED FOR  :: "+ofToString(event.id));
        if (users.count(event.id) > 0)
        {
            delete users[event.id];
            users.erase(event.id);
        }
    }
    else if (event.userStatus == USER_CALIBRATION_STOPPED) {
        ofLog(OF_LOG_NOTICE, "OpenNI: CALIB STOPPED FOR  :: "+ofToString(event.id));
    }
    else if (event.userStatus == USER_CALIBRATION_STARTED) {
        ofLog(OF_LOG_NOTICE, "OpenNI: CALIB STARTED FOR  :: "+ofToString(event.id));
    }
    else if (event.userStatus == USER_SKELETON_LOST) {
        ofLog(OF_LOG_NOTICE, "OpenNI: SKEL LOST FOR  :: "+ofToString(event.id));
    }
    else if (event.userStatus == USER_SKELETON_FOUND) {
        ofLog(OF_LOG_NOTICE, "OpenNI: SKEL FOUNd FOR  :: "+ofToString(event.id));
    }
}


////////////////////////////////////////////////////////////////
//  Calibration

void OpenNI::enableCalibration(ofxSecondWindow & window)
{
    this->window = &window;
    toggleCalibrate->setActive(true);
}

void OpenNI::eventAddPointPairs(GuiButtonEventArgs & e)
{
    vector<cv::Point2f> & cvPoints = calibration.getChessboardCorners();
    vector<ofVec3f> worldPoints;
    for (int i=0; i<cvPoints.size(); i++)
    {
        ofVec3f worldPoint = getWorldCoordinateAt(cvPoints[i].x, cvPoints[i].y);
        if (worldPoint.z > 0) {
            worldPoints.push_back(worldPoint);
        }
    }
    calibration.addPointPairs(worldPoints);
}

void OpenNI::eventCalibrate(GuiButtonEventArgs & e)
{
    calibration.calibrate(kpt);
}

void OpenNI::eventSaveCalibration(GuiButtonEventArgs & e)
{
    calibration.saveCalibration(kpt);
}

void OpenNI::eventLoadCalibration(GuiButtonEventArgs & e)
{
    calibration.loadCalibration(kpt);
}

void OpenNI::saveCalibration(string filename)
{
    kpt.saveCalibration(filename);
}

void OpenNI::loadCalibration(string filename)
{
    kpt.loadCalibration(filename);
}

void OpenNI::startCalibrationModule()
{
    setTrackingUsers(false);
    setTrackingContours(false);
    calibrating = true;
    
    kinect.addImageGenerator();
    calibration.setup(window->getWidth(), window->getHeight());
    updateCalibration();
    
    panelCalibration->setActive(true);
}

void OpenNI::stopCaibrationModule()
{
    calibrating = false;
    calibration.stop();
    
    kinect.removeImageGenerator();
    panelCalibration->setActive(false);
}

void OpenNI::updateCalibration()
{
    rgbImage.setFromPixels(kinect.getImagePixels());
    if (calibration.getTesting()) {
        testCalibration();
    }
    else {
        calibration.searchForCorners(rgbImage);
    }
}

void OpenNI::testCalibration()
{
    ofPoint testPoint(ofClamp(ofGetMouseX()-200, 0, kinect.getWidth()-1), ofClamp(ofGetMouseY(), 0, kinect.getHeight()-1));
    ofVec3f worldPoint = getWorldCoordinateAt(testPoint.x, testPoint.y);
    ofVec2f projectedPoint = kpt.getProjectedPoint(worldPoint);
    calibration.drawTestingPoint(projectedPoint);
}

void OpenNI::drawCalibration()
{
    kinect.drawImage(200, 0);
    calibration.draw(window);
}

