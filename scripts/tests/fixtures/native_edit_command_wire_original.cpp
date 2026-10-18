void LLSelectMgr::sendListToRegions(LLObjectSelectionHandle selected_handle,
                                    const std::string& message_name,
                                    void (*pack_header)(void *user_data),
                                    void (*pack_body)(LLSelectNode* node, void *user_data),
                                    void (*log_func)(LLSelectNode* node, void *user_data),
                                    void *user_data,
                                    ESendType send_type)
{
    LLSelectNode* node;
    LLSelectNode* linkset_root = NULL;
    LLViewerRegion* last_region;
    LLViewerRegion* current_region;
    S32 objects_in_this_packet = 0;

    bool link_operation = message_name == "ObjectLink";

    if (mAllowSelectAvatar)
    {
        if (selected_handle->getObjectCount() == 1
            && selected_handle->getFirstObject() != NULL
            && selected_handle->getFirstObject()->isAvatar())
        {
            // Server doesn't move avatars at the moment, it is a local debug feature,
            // but server does update position regularly, so do not drop mLastPositionLocal
            // Position override for avatar gets reset in LLAgentCamera::resetView().
        }
        else
        {
            // drop mLastPositionLocal (allow next update through)
            resetObjectOverrides(selected_handle);
        }
    }
    else
    {
        //clear update override data (allow next update through)
        resetObjectOverrides(selected_handle);
    }

    std::queue<LLSelectNode*> nodes_to_send;

    struct push_all : public LLSelectedNodeFunctor
    {
        std::queue<LLSelectNode*>& nodes_to_send;
        push_all(std::queue<LLSelectNode*>& n) : nodes_to_send(n) {}
        virtual bool apply(LLSelectNode* node)
        {
            if (node->getObject())
            {
                nodes_to_send.push(node);
            }
            return true;
        }
    };
    struct push_some : public LLSelectedNodeFunctor
    {
        std::queue<LLSelectNode*>& nodes_to_send;
        bool mRoots;
        push_some(std::queue<LLSelectNode*>& n, bool roots) : nodes_to_send(n), mRoots(roots) {}
        virtual bool apply(LLSelectNode* node)
        {
            if (node->getObject())
            {
                bool is_root = node->getObject()->isRootEdit();
                if ((mRoots && is_root) || (!mRoots && !is_root))
                {
                    nodes_to_send.push(node);
                }
            }
            return true;
        }
    };
    struct push_all  pushall(nodes_to_send);
    struct push_some pushroots(nodes_to_send, true);
    struct push_some pushnonroots(nodes_to_send, false);

    switch(send_type)
    {
      case SEND_ONLY_ROOTS:
          if(message_name == "ObjectBuy")
            selected_handle->applyToRootNodes(&pushroots);
          else
            selected_handle->applyToRootNodes(&pushall);

        break;
      case SEND_INDIVIDUALS:
        selected_handle->applyToNodes(&pushall);
        break;
      case SEND_ROOTS_FIRST:
        // first roots...
        selected_handle->applyToNodes(&pushroots);
        // then children...
        selected_handle->applyToNodes(&pushnonroots);
        break;
      case SEND_CHILDREN_FIRST:
        // first children...
        selected_handle->applyToNodes(&pushnonroots);
        // then roots...
        selected_handle->applyToNodes(&pushroots);
        break;

    default:
        LL_ERRS() << "Bad send type " << send_type << " passed to SendListToRegions()" << LL_ENDL;
    }

    // bail if nothing selected
    if (nodes_to_send.empty())
    {
        return;
    }

    node = nodes_to_send.front();
    nodes_to_send.pop();

    // cache last region information
    current_region = node->getObject()->getRegion();

    // Start duplicate message
    // CRO: this isn't
    gMessageSystem->newMessage(message_name.c_str());
    (*pack_header)(user_data);

    // For each object
    while (node != NULL)
    {
        // remember the last region, look up the current one
        last_region = current_region;
        current_region = node->getObject()->getRegion();

        // if to same simulator and message not too big
        if ((current_region == last_region)
            && (! gMessageSystem->isSendFull(NULL))
            && (objects_in_this_packet < MAX_OBJECTS_PER_PACKET))
        {
            if (link_operation && linkset_root == NULL)
            {
                // linksets over 254 will be split into multiple messages,
                // but we need to provide same root for all messages or we will get separate linksets
                linkset_root = node;
            }
            // add another instance of the body of the data
            (*pack_body)(node, user_data);
            // do any related logging
            (*log_func)(node, user_data);
            ++objects_in_this_packet;

            // and on to the next object
            if(nodes_to_send.empty())
            {
                node = NULL;
            }
            else
            {
                node = nodes_to_send.front();
                nodes_to_send.pop();
            }
        }
        else
        {
            // otherwise send current message and start new one
            gMessageSystem->sendReliable( last_region->getHost());
            objects_in_this_packet = 0;

            gMessageSystem->newMessage(message_name.c_str());
            (*pack_header)(user_data);

            if (linkset_root != NULL)
            {
                if (current_region != last_region)
                {
                    // root should be in one region with the child, reset it
                    linkset_root = NULL;
                }
                else
                {
                    // add root instance into new message
                    (*pack_body)(linkset_root, user_data);
                    ++objects_in_this_packet;
                }
            }

            // don't move to the next object, we still need to add the
            // body data.
        }
    }

    // flush messages
    if (gMessageSystem->getCurrentSendTotal() > 0)
    {
        gMessageSystem->sendReliable( current_region->getHost());
    }
    else
    {
        gMessageSystem->clearMessage();
    }

    // LL_INFOS() << "sendListToRegions " << message_name << " obj " << objects_sent << " pkt " << packets_sent << LL_ENDL;
}