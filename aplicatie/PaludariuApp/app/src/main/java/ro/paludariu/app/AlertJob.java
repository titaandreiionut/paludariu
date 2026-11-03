package ro.paludariu.app;

import android.app.job.JobParameters;
import android.app.job.JobService;

/** Pornita de Android cam la fiecare 15 minute, cand telefonul e pe WiFi. */
public class AlertJob extends JobService {
    @Override
    public boolean onStartJob(JobParameters params) {
        new Thread(() -> {
            AlertChecker.checkAll(getApplicationContext());
            jobFinished(params, false);
        }).start();
        return true;   // lucram pe alt fir; anuntam singuri cand am terminat
    }

    @Override
    public boolean onStopJob(JobParameters params) {
        return true;   // daca Android opreste verificarea, o reia mai tarziu
    }
}
