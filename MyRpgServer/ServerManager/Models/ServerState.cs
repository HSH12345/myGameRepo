namespace RpgServerManager.Models;
public enum ServerState
{
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    FAILED,
    EXTERNAL,
    MISCONFIGURED
}

