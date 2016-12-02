#include "face_detection_tracker.h"

// Initialize static members.
bool FaceDetectionTracker::m_newBB_static = false;

FaceDetectionTracker::FaceDetectionTracker() :
    m_it(m_node)
{
    ///////////////////////
    /// Detection part. ///
    ///////////////////////

    // Subscribe to input video feed and publish output video feed.
    m_imageSub = m_it.subscribe("/pseye_camera/image_raw", 1, &FaceDetectionTracker::imageCallback, this);


    // Load the cascades.
    // // Frontal face.
    if(!m_frontalfaceCascade.load(m_directory + m_frontalFaceCascadeName))
    {
        ROS_ERROR("Error loading frontal face cascade!");
        return;
    }

    // Profile face.
    if(!m_profilefaceCascade.load(m_directory + m_profilefaceCascadeName))
    {
        ROS_ERROR("Error loading profile face cascade!");
        return;
    }
    skipFrames = 0;

    /////////////////////
    /// Tracker part. ///
    /////////////////////

    bbPub = m_node.advertise<perception_msgs::Rect>("tracker/bb", m_queuesize);

    m_paras.enableTrackingLossDetection = true;
    // paras.psrThreshold = 10; // lower more flexible
    m_paras.psrThreshold = 13.5; // higher more restricted to changes
    m_paras.psrPeakDel = 2; // 1;

    timeout = ros::Duration(2);

    model = createFisherFaceRecognizer();
    //model = createEigenFaceRecognizer();

    // size of images should be positive
    pic_size.x = -1; pic_size.y = -1;

    readImages("person0/",1); //Andrej
    readImages("person1/",2); //Bjoern
    readImages("person2/",3); //Hidu
    readImages("person3/",4); //Neda

    my_map = {
        {0, "unknown" },
        {1, "Andrej" },
        {2, "Bjoern" },
        {3, "Hidu" },
        {4, "Neda" }
    };
    ROS_INFO("Done reading face images, training the model now");

    //TODO
    index_list = 0;
    std::fill_n(myints, LIST_SIZE, -1);

    ros::Time tic = ros::Time::now();
    model->train(images, labels);
    ros::Time toc = ros::Time::now();
    ROS_INFO("Time of training was %.2fs", (toc-tic).toSec() );
}

FaceDetectionTracker::~FaceDetectionTracker()
{}

///////////////////////
/// Detection part. ///
///////////////////////

void FaceDetectionTracker::imageCallback(const sensor_msgs::ImageConstPtr &msg)
{
    // Convert the message to cv image.
    try
    {
        m_cvPtr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);

        // Resize the image to half its size.
        //cv::resize(m_cvPtr->image, m_cvPtr->image, cv::Size(m_cvPtr->image.cols / 2, m_cvPtr->image.rows / 2));
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    // Apply the classifiers to the frame.
    if(m_cvPtr)
    {
        // detection part
        detectAndDisplay(m_cvPtr->image);
        // We can always track when we have new image.
        track();
    }
    else
    {
        ROS_INFO("No captured frame!");
    }
}


void FaceDetectionTracker::detectAndDisplay(cv::Mat frame)
{
    std::vector<Rect> faces;
    Mat frameGray;

    cv::cvtColor(frame, frameGray, CV_BGR2GRAY);
    cv::equalizeHist(frameGray, frameGray);

    faceMethod = 0;

    if (skipFrames < 0 )
    {
        m_frontalfaceCascade.detectMultiScale(frameGray, faces, 1.3, 3 );
        if (faces.size() > 0)
        {
            faceMethod = 1;
        }
        /*
        if (faces.size() == 0)
        {
            m_profilefaceCascade.detectMultiScale(frameGray, faces, 1.3, 3 );
            if (faces.size() > 0)
            {
                faceMethod = 2;
            }
        }*/
        if (faces.size() > 0)
        {
            // face was just deteced on a picture, skipping next SKIP_FRAMES pictures
            // for CPU optimization   
            skipFrames = SKIP_FRAMES;
        }
        else
        {
            skipFrames--;
        }    
    }
    else
    {
        skipFrames--;
    }

    //ROS_INFO("%d ", faceMethod);
    //for( size_t i = 0; i < faces.size(); i++ )
    i=0;
    if (faces.size() > 0)
    {
        // Point in the upper left corner.
        m_p1 = cv::Point(faces[i].x, faces[i].y);

        // Point in the lower right corner.
        m_p2 = cv::Point(faces[i].x + faces[i].width, faces[i].y + faces[i].height);

        m_width = faces[i].width;
        m_height = faces[i].height;

        // Signal a new bounding box.
        m_newBB_static = true;

        cv::Rect R(m_p1, m_p2);
        cv::Mat tmpMat;
        //ROS_INFO("1 [%d, %d], dim %d", frameGray.rows, frameGray.cols, frameGray.dims);
        cv::resize(frameGray(R), tmpMat, cv::Size(100,100));  //TODO get size from Database
        //ROS_INFO("2 [%d, %d], dim %d", tmpMat.rows, tmpMat.cols, tmpMat.dims);
        int predictedLabel = model->predict(tmpMat);
        //ROS_INFO("Name %s", my_map[predictedLabel].c_str());
        myints[index_list] = predictedLabel;
        index_list = (index_list + 1) % LIST_SIZE;

        //find most frequent element
        int tmpListFreq[] = {0,0,0,0,0};
        for (int j=0; j < LIST_SIZE; j++)
        {
            tmpListFreq[myints[j]]++;
        }
        int myMax = -1; myMaxIndex = -1;
        for (int j=0; j < 5; j++)
        {
            if (myMax < tmpListFreq[j] )
            {
                myMax = tmpListFreq[j];
                myMaxIndex = j;
            }
        }
        //ROS_INFO("Global names %d %s", myMaxIndex, my_map[myMaxIndex].c_str());


#ifdef DEBUG // Enable/Disable in the header.
        cv::Mat out_img;
        // we should not edit the frame, because it is poiter
        frame.copyTo(out_img);
        // Visualize the image with the fame.

        switch(faceMethod)
        {
            case 2:
            {
                std::string box_text = format("# profiles = %d", faces.size());
                cv::putText(out_img, box_text, Point(10, 10), FONT_HERSHEY_PLAIN, 1.0, CV_RGB(0,255,0), 2.0);
                cv::putText(out_img, my_map[predictedLabel].c_str(), m_p1, FONT_HERSHEY_PLAIN, 1.0, CV_RGB(0,255,0), 1.0);
                cv::rectangle(out_img, m_p1, m_p2, CV_RGB(0, 255, 0), 4, 8, 0);
                break;
            }
            case 1:
            {
                std::string box_text = format("# frontal = %d", faces.size());
                cv::putText(out_img, box_text, Point(10, 10), FONT_HERSHEY_PLAIN, 1.0, CV_RGB(255,255,0), 2.0);
                cv::putText(out_img, my_map[predictedLabel].c_str(), cv::Point(m_p1.x+10, m_p1.y+20), FONT_HERSHEY_PLAIN, 1.0, CV_RGB(255,255,0), 1.0);

                cv::rectangle(out_img, m_p1, m_p2, CV_RGB(255, 255, 0), 4, 8, 0);
            }
        }
        cv::imshow( m_windowName, out_img);
        cv::waitKey(33);
#endif
    }
}

/////////////////////
/// Tracker part. ///
/////////////////////
void FaceDetectionTracker::track()
{

    // If new bounding box arrived (detected face) && we are not yet tracking anything.
    if (m_newBB_static && !tracking)
    {
        ROS_INFO("New bounding box!");
        // Create new tracker!
        cKCF = new cf_tracking::KcfTracker(m_paras);
        // Save the incoming bounding box to a private member.
        bb.x = m_p1.x; 
        bb.y = m_p1.y; 
        bb.height = m_height; 
        bb.width = m_width; 

        // Reinitialize the tracker.
        if (cKCF->reinit(m_cvPtr->image, bb)) // KcfTracker->reinit(cv::Mat, cv::Rect)
        {
            // This means that it is correctly initalized.
            tracking = true;
            targetOnFrame = true;
            start_time = ros::Time::now();
        }
        else
        {
            // The tracker initialization has failed.
            delete cKCF;
            tracking = false;
            targetOnFrame = false;
            skipFrames = -1;
        }
    }

    // If the target is on frame.
    if (targetOnFrame)
    {
        /*bb.x = m_p1.x;
        bb.y = m_p2.y; 
        bb.width = m_width; 
        bb.height = m_height;*/ 
        // Update the current tracker (if we have one)!
        targetOnFrame = cKCF->update(m_cvPtr->image, bb); 
        // If the tracking has been lost or the bounding box is out of limits.
        if (!targetOnFrame)
        {
            // We are not tracking anymore.
            delete cKCF;
            tracking = false;
        }
    }
    
    if (ros::Time::now() - start_time > timeout) 
    {
            //ROS_INFO("Just reseted****************************************************");
            start_time = ros::Time::now();
            // we need to implement reseting the window without a long delay.
    }

    // If we are tracking, then publish the bounding box.
    if (tracking)
    {
        m_outBb.x = bb.x;
        m_outBb.y = bb.y;
        m_outBb.width = bb.width;
        m_outBb.height = bb.height;
        bbPub.publish(m_outBb);
    }


#ifdef DEBUG // Enable/Disable in the header.
    cv::Mat out_img;
    cv::cvtColor(m_cvPtr->image, out_img, CV_BGR2GRAY);// Convert to gray scale
    cv::cvtColor(out_img, out_img, CV_GRAY2BGR); //Convert from 1 color channel to 3 (trick)

    //Draw a rectangle on the out_img using the tracked bounding box.
    if (targetOnFrame)
    {
        cv::rectangle(out_img, cv::Point(bb.x, bb.y), cv::Point(bb.x + bb.width, bb.y + bb.height), CV_RGB(255, 0, 0));
        cv::putText(out_img, my_map[myMaxIndex].c_str(), cv::Point(bb.x+10, bb.y+20), FONT_HERSHEY_PLAIN, 1.0, CV_RGB(255,0,0), 1.0);
    }
    //std::string box_text = format("# tracked = %d", -1);
    //cv::putText(out_img, box_text, Point(10, 10), FONT_HERSHEY_PLAIN, 1.0, CV_RGB(255,0,0), 2.0);

    cv::imshow(m_windowName0, out_img);
    cv::waitKey(3);
#endif

    // Signal that the image and bounding box are not new.
    m_newBB_static = false;
}


Mat FaceDetectionTracker::norm_0_255(InputArray _src) {
    Mat src = _src.getMat();
    // Create and return normalized image:
    Mat dst;
    switch(src.channels()) {
    case 1:
        cv::normalize(_src, dst, 0, 255, NORM_MINMAX, CV_8UC1);
        break;
    case 3:
        cv::normalize(_src, dst, 0, 255, NORM_MINMAX, CV_8UC3);
        break;
    default:
        src.copyTo(dst);
        break;
    }
    return dst;
}
bool  FaceDetectionTracker::readImages(std::string person, int tag)
{
    std::string dirNameTmp = dirName;
    dirNameTmp.append(person);
    //ROS_INFO("Directory: %s, %s",dirName.c_str(), dirNameTmp.c_str());
    dir = opendir( dirNameTmp.c_str() );
    while ((ent = readdir (dir)) != NULL)
    {
        // . and .. needs to be rejected
        std::string tmpDots = ent->d_name;
        if(tmpDots.compare(".")!= 0 && tmpDots.compare("..")!= 0)
        {
            std::string imgPath(dirNameTmp + tmpDots);
            Mat tmpImg = imread(imgPath);
            if ((tmpImg.rows > 0 && tmpImg.cols > 0) && (pic_size.x == -1 && pic_size.y == -1) )
            {
                pic_size.x = tmpImg.cols;
                pic_size.y = tmpImg.rows;
                ROS_INFO("Size of face images [y: %d, x: %d]", pic_size.y, pic_size.x);
            }
            if ( (tmpImg.cols != pic_size.x) || (tmpImg.rows != pic_size.y) )
            {
                ROS_ERROR("Images are not the same size, program will die.");
                return false;
            }
            images.push_back(imread(imgPath, 0)); //load grayscale images
            labels.push_back(tag);
        }
    }
    return true;
}
