#include "btdata.h"
#include "util.h"


// ��ȷ�Ĺرտͻ���
void client_shutdown(int sig)
{
    // ����ȫ��ֹͣ������ֹͣ���ӵ�����peer, �Լ���������peer������. Set global stop variable so that we stop trying to connect to peers and
    // �����������peer���ӵ��׽��ֺ����ӵ�����peer���߳�.
    exit(0);
}
