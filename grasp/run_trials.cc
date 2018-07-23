/*!
    \file grasp/run_trials.cc
    \brief Performs grasp trials

    TODO

    \author João Borrego : jsbruglie
*/

#include "run_trials.hh"

// Globals are used for callbacks

/// Global timeout flag
bool g_timeout  {false};
/// Mutex for global timeout flag
std::mutex g_timeout_mutex;
/// Global object resting flag
bool g_resting  {false};
/// Mutex for global object resting flag
std::mutex g_resting_mutex;
/// Global object resting pose
ignition::math::Pose3d g_pose {0,0,0,0,0,0};
/// Global setp finished flag
bool g_finished {false};
/// Mutex for global step finished flag
std::mutex g_finished_mutex;
/// Global trial outcome
bool g_success  {false};

/// Robot name - TODO - make parameter
#ifdef SHADOWHAND
std::string g_hand_name {"shadowhand"};
#endif
#ifndef SHADOWHAND
std::string g_hand_name {"vizzy_hand"};
#endif

int main(int _argc, char **_argv)
{
    // List of grasp targets
    std::vector<std::string> targets;

    // Command-line args
    std::string obj_cfg_dir, grasp_cfg_dir, out_img_dir, out_trials_dir, robot;
    parseArgs(_argc, _argv,
        obj_cfg_dir, grasp_cfg_dir, out_img_dir, out_trials_dir, robot);

    // Obtain target objects
    obtainTargets(targets, obj_cfg_dir);
    if (targets.empty()) {
        errorPrintTrace("No valid objects retrieved from file");
        exit(EXIT_FAILURE);
    }

    // Load gazebo as a client
    gazebo::client::setup(_argc, _argv);

    // Setup communication
    gazebo::transport::NodePtr node(new gazebo::transport::Node());
    std::map<std::string, gazebo::transport::PublisherPtr> pubs;
    std::map<std::string, gazebo::transport::SubscriberPtr> subs;
    setupCommunications(node, pubs, subs);
    
    // Spawn camera
    /*
    std::string camera_name("rgbd_camera");
    std::string camera_filename = "model://" + camera_name;
    ignition::math::Pose3d camera_pose(0,0,0.8,0,1.57,0);
    spawnModelFromFilename(pubs["factory"], camera_pose, camera_filename);
    pubs["camera"]->WaitForConnection();
    debugPrintTrace("Camera connected");
    */

    // TODO: Foreach object

    // Initial hand pose
    ignition::math::Pose3d hand_pose(0,0,0.8,0,0,0);
    // Initial target object pose
    ignition::math::Pose3d model_pose(0,0,0.1,0,0,0);

    std::string model_filename;

    for (auto const &model_name : targets)
    {
        model_filename = "model://" + model_name;
        
        // Obtain candidate grasps
        std::string cfg_file(grasp_cfg_dir + 
            model_name + "." + robot + ".grasp.yml");
        std::vector<Grasp> grasps;
        obtainGrasps(cfg_file, robot, grasps);
        if (grasps.empty()) {
            errorPrintTrace("No valid grasps retrieved from file");
            exit(EXIT_FAILURE);
        }

        // Reset hand pose so it won't collide with target object
        setPose(pubs["hand"], hand_pose, 2.0);
        while (waitingTrigger(g_timeout_mutex, g_timeout)) {waitMs(10);}
        liftHand(pubs["hand"]); // TODO

        // Spawn object
        spawnModelFromFilename(pubs["factory"], model_pose, model_filename);
        pubs["target"]->WaitForConnection();
        debugPrintTrace("Target connected");

        // Obtain object resting position
        getTargetPose(pubs["target"], true);
        while (waitingTrigger(g_resting_mutex, g_resting)) {waitMs(10);}
    
        // Perform trials
        for (auto candidate : grasps)
        {
            tryGrasp(candidate, pubs, model_name);
        }

        // Cleanup
        removeModel(pubs["request"], model_name);
    }

    // TODO - Render RGBD camera frames

    // Capture and render frame
    /*
    captureFrame(pubs["camera"]);
    while (waitingTrigger(g_finished_mutex, g_finished)) {waitMs(10);}    
    */

    // Cleanup
    /*
    removeModel(pubs["request"], camera_name);
    */
    setPose(pubs["hand"], hand_pose);

    // Shut down
    gazebo::client::shutdown();
    return 0;
}

//////////////////////////////////////////////////
const std::string getUsage(const char* argv_0)
{
    return \
        "usage:   " + std::string(argv_0) + " [options]\n" +
        "options: -d <object dataset yaml>\n"  +
        "         -g <grasp candidates yaml>\n" +
        "         -i <image output directory>\n" +
        "         -o <dataset output directory>\n" +
        "         -r <robot>\n";
}

//////////////////////////////////////////////////
void parseArgs(
    int argc,
    char** argv,
    std::string & obj_cfg_dir,
    std::string & grasp_cfg_dir,
    std::string & out_img_dir,
    std::string & out_trials_dir,
    std::string & robot)
{
    int opt;
    bool d, g, i, o, r;

    while ((opt = getopt(argc,argv,"d: g: i: o: r:")) != EOF)
    {
        switch (opt)
        {
            case 'd':
                d = true; obj_cfg_dir = optarg;    break;
            case 'g':
                g = true; grasp_cfg_dir = optarg;  break;
            case 'i':
                i = true; out_img_dir = optarg;    break;
            case 'o':
                o = true; out_trials_dir = optarg; break;
            case 'r':
                r = true; robot = optarg; break;
            case '?':
                std::cerr << getUsage(argv[0]);
            default:
                std::cout << std::endl;
                exit(EXIT_FAILURE);
        }
    }

    if (!d || !g || !i || !o || !r) {
        std::cerr << getUsage(argv[0]);
        exit(EXIT_FAILURE);
    }

    debugPrint("Parameters:\n" <<
        "   Object dataset yaml      '" << obj_cfg_dir << "'\n" <<
        "   Grasp candidates yaml    '" << grasp_cfg_dir << "'\n" <<
        "   Image output directory   '" << out_img_dir << "'\n" <<
        "   Dataset output directory '" << out_trials_dir << "'\n" <<
        "   Robot                    '" << robot << "'\n");
}

/////////////////////////////////////////////////
void setupCommunications(
    gazebo::transport::NodePtr & node,
    std::map<std::string, gazebo::transport::PublisherPtr> & pubs,
    std::map<std::string, gazebo::transport::SubscriberPtr> & subs)
{
    // Create the communication node
    node->Init();

    // Instance publishers and subscribers
    pubs["hand"] = node->Advertise<HandMsg>(HAND_REQ_TOPIC);
    subs["hand"] = node->Subscribe(HAND_RES_TOPIC, onHandResponse);
    pubs["target"] = node->Advertise<TargetRequest>(TARGET_REQ_TOPIC);
    subs["target"] = node->Subscribe(TARGET_RES_TOPIC, onTargetResponse);
    pubs["contact"] = node->Advertise<ContactRequest>(CONTACT_REQ_TOPIC);
    subs["contact"] = node->Subscribe(CONTACT_RES_TOPIC, onContactResponse);
    pubs["camera"] = node->Advertise<CameraRequest>(CAMERA_REQ_TOPIC);
    subs["camera"] = node->Subscribe(CAMERA_RES_TOPIC, onCameraResponse);
    pubs["factory"] = node->Advertise<gazebo::msgs::Factory>(FACTORY_TOPIC);
    pubs["request"] = node->Advertise<gazebo::msgs::Request>(REQUEST_TOPIC);

    // Wait for subscribers to connect
    pubs["factory"]->WaitForConnection();
    pubs["request"]->WaitForConnection();
    pubs["contact"]->WaitForConnection();
    debugPrintTrace("Gazebo connected");
    pubs["hand"]->WaitForConnection();
    debugPrintTrace("Hand connected");
}

/////////////////////////////////////////////////
void obtainTargets(std::vector<std::string> & targets,
    const std::string & file_name)
{
    std::string mesh, name, path;

    try
    {
        YAML::Node config = YAML::LoadFile(file_name);
        for (const auto & kv_pair : config)
        {
            // TODO? - Use proper object for target object data
            name = kv_pair.second["name"].as<std::string>();
            //mesh = kv_pair.second["mesh"].as<std::string>();
            //path = kv_pair.second["path"].as<std::string>();
            targets.push_back(name);
        }
    }
    catch (YAML::Exception& yamlException)
    {
        std::cerr << "Unable to parse " << file_name << "\n";
    }
    debugPrintTrace("Found " << targets.size() << " objects in " << file_name);
}

/////////////////////////////////////////////////
void setPose(gazebo::transport::PublisherPtr pub,
    ignition::math::Pose3d pose,
    double timeout)
{
    HandMsg msg;
    gazebo::msgs::Pose *pose_msg = new gazebo::msgs::Pose();
    gazebo::msgs::Set(pose_msg, pose);
    msg.set_allocated_pose(pose_msg);
    if (timeout > 0) {
        msg.set_timeout(timeout);
    }
    pub->Publish(msg);
}

/////////////////////////////////////////////////
void getContacts(gazebo::transport::PublisherPtr pub,
    const std::string & target,
    const std::string & hand)
{
    ContactRequest msg;
    CollisionRequest *msg_col_gnd = msg.add_collision();
    msg_col_gnd->set_collision1("ground_plane");
    msg_col_gnd->set_collision2(hand);
    CollisionRequest *msg_col_tgt = msg.add_collision();
    msg_col_tgt->set_collision1(target);
    msg_col_tgt->set_collision2(hand);
    pub->Publish(msg);
}

/////////////////////////////////////////////////
void closeFingers(gazebo::transport::PublisherPtr pub, double timeout)
{
    grasp::msgs::Hand msg;
    std::vector<std::string> joints;
    std::vector<double> values;
    double value = 1.57; 

    #ifdef SHADOWHAND // Shadowhand
    joints = {
        "rh_FFJ4","rh_FFJ3","rh_FFJ2",
        "rh_MFJ4","rh_MFJ3","rh_MFJ2",
        "rh_RFJ4","rh_RFJ3","rh_RFJ2",
        "rh_LFJ5","rh_LFJ4","rh_LFJ3","rh_LFJ2",
        "rh_THJ5","rh_THJ4","rh_THJ3","rh_THJ2",
    };
    values = {
        0.0, value, value,
        0.0, value, value,
        0.0, value, value,
        0.0, 0.0, value, value,
        0.0, 0.0, value, value,
    };
    #endif
    #ifndef SHADOWHAND // Vizzy
    joints = {
        "r_thumb_phal_1_joint",
        "r_ind_phal_1_joint",
        "r_med_phal_1_joint"
    };
    values = {value, value, value};
    #endif
    
    for (unsigned int i = 0; i < joints.size(); i++)
    {
        grasp::msgs::Target *target = msg.add_pid_targets();
        target->set_type(POSITION);
        target->set_joint(joints.at(i));
        target->set_value(values.at(i));
    }

    if (timeout > 0) { msg.set_timeout(timeout); }

    pub->Publish(msg);
}

/////////////////////////////////////////////////
void liftHand(gazebo::transport::PublisherPtr pub, double timeout)
{
    grasp::msgs::Hand msg;
    std::vector<std::string> joints;
    std::vector<double> values;

    joints = {
        "virtual_px_joint","virtual_py_joint", "virtual_pz_joint",
        "virtual_rr_joint","virtual_rp_joint", "virtual_ry_joint"
    };
    values = {0,0,0.8,0,0,0};

    for (unsigned int i = 0; i < joints.size(); i++)
    {
        grasp::msgs::Target *target = msg.add_pid_targets();
        target->set_type(POSITION);
        target->set_joint(joints.at(i));
        target->set_value(values.at(i));
    }
    if (timeout > 0) { msg.set_timeout(timeout); }
    pub->Publish(msg);
}

/////////////////////////////////////////////////
void getTargetPose(gazebo::transport::PublisherPtr pub, bool rest)
{
    TargetRequest msg;
    if (rest)   { msg.set_type(REQ_REST_POSE); }
    else        { msg.set_type(REQ_GET_POSE); }
    pub->Publish(msg);
}

/////////////////////////////////////////////////
void reset(gazebo::transport::PublisherPtr pub)
{
    HandMsg msg;
    msg.set_reset(true);
    pub->Publish(msg);
}

/////////////////////////////////////////////////
void tryGrasp(
    Grasp & grasp,
    std::map<std::string, gazebo::transport::PublisherPtr> & pubs,
    const std::string & target)
{
    // Get pose in world frame, given the object pose
    ignition::math::Pose3d hand_pose = grasp.getPose(g_pose);
    if (hand_pose.Pos().Z() < 0) {
        debugPrintTrace("Pose beneath ground plane. Aborting grasp");
        return;
    }

    debugPrintTrace("Candidate " << hand_pose << " Target " << g_pose);

    // Teleport hand to grasp candidate pose
    // Add the resting position transformation first
    setPose(pubs["hand"], hand_pose, 0.0001);
    while (waitingTrigger(g_timeout_mutex, g_timeout)) {waitMs(10);}
    debugPrintTrace("Hand moved to grasp candidate pose");
    // Check if hand is already in collision
    getContacts(pubs["contact"], target, g_hand_name);
    while (waitingTrigger(g_finished_mutex, g_finished)) {waitMs(10);}
    if (!g_success) {
        grasp.success = false;
        debugPrintTrace("Collisions detected. Aborting grasp");
        return;
    }
    debugPrintTrace("No collisions detected");
    // Close fingers
    closeFingers(pubs["hand"], 0.5);
    while (waitingTrigger(g_timeout_mutex, g_timeout)) {waitMs(10);}
    debugPrintTrace("Fingers closed");
    // Lift object
    liftHand(pubs["hand"], 0.7);
    while (waitingTrigger(g_timeout_mutex, g_timeout)) {waitMs(10);}
    debugPrintTrace("Object lifted");
    // Stop lifting
    //setVelocity(pubs["hand"], velocity_stop, 0.5);
    //while (waitingTrigger(g_timeout_mutex, g_timeout)) {waitMs(10);}
    // Check if object was grasped
    // TODO - Replace by collision check
    getTargetPose(pubs["target"], false);
    while (waitingTrigger(g_finished_mutex, g_finished)) {waitMs(10);}

    debugPrintTrace("Success: " << g_success
        << " - Pose: " << grasp.pose);

    grasp.success = g_success;
    reset(pubs["hand"]);

    waitMs(50);
}

/////////////////////////////////////////////////
void captureFrame(gazebo::transport::PublisherPtr pub)
{
    CameraRequest msg;
    msg.set_type(REQ_CAPTURE);
    pub->Publish(msg);
}

//////////////////////////////////////////////////
bool waitingTrigger(std::mutex & mutex, bool & trigger)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (trigger) {
        trigger = false;
        return false;
    }
    return true;
}

/////////////////////////////////////////////////
void onHandResponse(HandMsgPtr & _msg)
{
    if (_msg->has_timeout()) {
        std::lock_guard<std::mutex> lock(g_timeout_mutex);
        g_timeout = true;
    }
}

/////////////////////////////////////////////////
void onTargetResponse(TargetResponsePtr & _msg)
{
    debugPrintTrace("Target plugin response");
    if (_msg->has_pose()) {
        if (_msg->type() == RES_POSE)
        {
            bool success = false;
            ignition::math::Pose3d pose(gazebo::msgs::ConvertIgn(_msg->pose()));
            if (pose.Pos().Z() > Z_LIFTED) {
                success = true;
            }
            std::lock_guard<std::mutex> lock(g_finished_mutex);
            g_pose = gazebo::msgs::ConvertIgn(_msg->pose());
            g_finished = true;
            g_success = success;
        }
        else if (_msg->type() == RES_REST_POSE)
        {
            std::lock_guard<std::mutex> lock(g_resting_mutex);
            g_pose = gazebo::msgs::ConvertIgn(_msg->pose());
            g_resting = true;
        }
    }
}

/////////////////////////////////////////////////
void onContactResponse(ContactResponsePtr & _msg)
{
    debugPrintTrace("Contact plugin response");
    std::lock_guard<std::mutex> lock(g_finished_mutex);
    g_finished = true;
    g_success = (_msg->contacts_size() == 0);
}

/////////////////////////////////////////////////
void onCameraResponse(CameraResponsePtr & _msg)
{
    debugPrintTrace("Camera plugin response");
    std::lock_guard<std::mutex> lock(g_finished_mutex);
    g_finished = true;
}

/////////////////////////////////////////////////
void inline waitMs(int delay)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}
